#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "bootloader_common.h"
#include "esp_flash_partitions.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_wifi.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <memory>

namespace {
/** Parse major.minor.patch; accepts optional leading 'v'/'V' (GitHub release tags). */
bool parseSemVer3(const char* version, int& major, int& minor, int& patch) {
  if (version == nullptr || version[0] == '\0') {
    return false;
  }
  if (version[0] == 'v' || version[0] == 'V') {
    ++version;
  }
  return sscanf(version, "%d.%d.%d", &major, &minor, &patch) == 3;
}

}  // namespace

struct InstallCallbackClearer {
  OtaUpdater* u;
  explicit InstallCallbackClearer(OtaUpdater* self) : u(self) {}
  ~InstallCallbackClearer() {
    u->installProgressCallback = nullptr;
    u->installProgressCallbackCtx = nullptr;
  }
};

void OtaUpdater::setInstallProgressCallback(void (*cb)(void*), void* ctx) {
  installProgressCallback = cb;
  installProgressCallbackCtx = ctx;
}

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/teerarattanapon/crosspoint-reader/releases/latest";
/* GitHub release JSON is ~7–8KB; cap avoids mid-TLS calloc of Content-Length (heap fragmentation). */
constexpr int kReleaseJsonCap = 12288;
/* Smaller HTTP buffers for metadata fetch — leave contiguous DRAM for local_buf. */
constexpr int kReleaseHttpBuf = 2048;

/* This is buffer and size holder to keep upcoming data from latestReleaseUrl */
char* local_buf;
int output_len;
int local_buf_cap;

/*
 * When esp_crt_bundle.h included, it is pointing wrong header file
 * which is something under WifiClientSecure because of our framework based on arduno platform.
 * To manage this obstacle, don't include anything, just extern and it will point correct one.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

/* Captured from HTTP_EVENT_ON_HEADER during redirect resolve (malloc'd). */
char* redirectLocation = nullptr;

esp_err_t redirect_event_handler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_HEADER || event->header_key == nullptr || event->header_value == nullptr) {
    return ESP_OK;
  }
  /* Case-insensitive "Location" — esp_http_client_get_header often misses this after fetch_headers. */
  if (strcasecmp(event->header_key, "Location") != 0) {
    return ESP_OK;
  }
  const size_t n = strlen(event->header_value);
  auto* buf = static_cast<char*>(malloc(n + 1));
  if (buf == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  memcpy(buf, event->header_value, n + 1);
  free(redirectLocation);
  redirectLocation = buf;
  return ESP_OK;
}

/**
 * GitHub release asset URLs 302 to release-assets.githubusercontent.com with a very long
 * Location header. Resolving that hop before esp_https_ota_begin avoids CONNECT failures
 * when the OTA client struggles with the redirect.
 *
 * After capturing Location we return immediately — a second open() to the CDN host was
 * failing with ESP_ERR_HTTP_CONNECT and is unnecessary before esp_https_ota_begin.
 */
bool resolveReleaseAssetUrl(std::string& url) {
  free(redirectLocation);
  redirectLocation = nullptr;

  esp_http_client_config_t cfg = {
      .url = url.c_str(),
      .timeout_ms = 30000,
      .disable_auto_redirect = true,
      .max_redirection_count = 0,
      .event_handler = redirect_event_handler,
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    LOG_ERR("OTA", "redirect resolve: client init failed");
    return false;
  }
  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    LOG_ERR("OTA", "redirect resolve open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);

  if (status == 200 || status == 206) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(redirectLocation);
    redirectLocation = nullptr;
    return true;
  }

  if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
    if (redirectLocation == nullptr || redirectLocation[0] == '\0') {
      LOG_ERR("OTA", "redirect resolve: missing Location (status %d)", status);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    url.assign(redirectLocation);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(redirectLocation);
    redirectLocation = nullptr;
    return true;
  }

  LOG_ERR("OTA", "redirect resolve: unexpected status %d", status);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  free(redirectLocation);
  redirectLocation = nullptr;
  return false;
}

esp_err_t event_handler(esp_http_client_event_t* event) {
  /* We do interested in only HTTP_EVENT_ON_DATA event only */
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

  if (local_buf == nullptr || local_buf_cap <= 1) {
    LOG_ERR("OTA", "HTTP body buffer missing");
    return ESP_ERR_NO_MEM;
  }

  const int space = local_buf_cap - 1 - output_len;
  if (space <= 0) {
    /* Truncation detected after perform via output_len vs content length. */
    return ESP_OK;
  }

  const int copy_len = min(event->data_len, space);
  if (copy_len > 0) {
    memcpy(local_buf + output_len, event->data, copy_len);
    output_len += copy_len;
  }
  return ESP_OK;
} /* event_handler */
} /* namespace */

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  JsonDocument filter;
  esp_err_t esp_err;
  JsonDocument doc;

  /* Pre-allocate before TLS/HTTP client buffers — calloc during ON_DATA was OOM'ing. */
  output_len = 0;
  local_buf_cap = kReleaseJsonCap;
  local_buf = static_cast<char*>(malloc(static_cast<size_t>(kReleaseJsonCap)));
  if (local_buf == nullptr) {
    local_buf_cap = 0;
    LOG_ERR("OTA", "Release JSON buffer alloc failed: %d", kReleaseJsonCap);
    return OOM_ERROR;
  }
  local_buf[0] = '\0';

  esp_http_client_config_t client_config = {
      .url = latestReleaseUrl,
      .event_handler = event_handler,
      .buffer_size = kReleaseHttpBuf,
      .buffer_size_tx = kReleaseHttpBuf,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  /* To track life time of local_buf, dtor will be called on exit from that function */
  struct localBufCleaner {
    char** bufPtr;
    int* capPtr;
    ~localBufCleaner() {
      if (*bufPtr) {
        free(*bufPtr);
        *bufPtr = NULL;
      }
      *capPtr = 0;
    }
  } localBufCleaner = {&local_buf, &local_buf_cap};

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    LOG_ERR("OTA", "HTTP Client Handle Failed");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_set_header(client_handle, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_set_header Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_perform(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_perform Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return HTTP_ERROR;
  }

  if (local_buf != nullptr && output_len >= 0 && output_len < local_buf_cap) {
    local_buf[output_len] = '\0';
  }

  /* esp_http_client_close will be called inside cleanup as well*/
  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_cleanup Failed : %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  if (output_len <= 0) {
    LOG_ERR("OTA", "Empty release JSON body");
    return JSON_PARSE_ERROR;
  }

  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;
  const DeserializationError error = deserializeJson(doc, local_buf, DeserializationOption::Filter(filter));
  if (error) {
    LOG_ERR("OTA", "JSON parse failed: %s", error.c_str());
    return JSON_PARSE_ERROR;
  }

  if (!doc["tag_name"].is<std::string>()) {
    LOG_ERR("OTA", "No tag_name found");
    return JSON_PARSE_ERROR;
  }

  if (!doc["assets"].is<JsonArray>()) {
    LOG_ERR("OTA", "No assets found");
    return JSON_PARSE_ERROR;
  }

  latestVersion = doc["tag_name"].as<std::string>();

  for (int i = 0; i < doc["assets"].size(); i++) {
    if (doc["assets"][i]["name"] == "firmware.bin") {
      otaUrl = doc["assets"][i]["browser_download_url"].as<std::string>();
      otaSize = doc["assets"][i]["size"].as<size_t>();
      totalSize = otaSize;
      updateAvailable = true;
      break;
    }
  }


  if (!updateAvailable) {
    LOG_ERR("OTA", "No firmware.bin asset found");
    return NO_UPDATE;
  }

  LOG_DBG("OTA", "Found update: %s", latestVersion.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  int currentMajor = 0, currentMinor = 0, currentPatch = 0;
  int latestMajor = 0, latestMinor = 0, latestPatch = 0;

  const auto currentVersion = CROSSPOINT_VERSION;

  // semantic version check (only match on 3 segments; GitHub tags may be "v1.2.3")
  const bool latestOk = parseSemVer3(latestVersion.c_str(), latestMajor, latestMinor, latestPatch);
  const bool currentOk = parseSemVer3(currentVersion, currentMajor, currentMinor, currentPatch);
  if (!latestOk || !currentOk) {
    LOG_ERR("OTA", "Version parse failed (latest=%s current=%s)", latestVersion.c_str(), currentVersion);
    return false;
  }

  /*
   * Compare major versions.
   * If they differ, return true if latest major version greater than current major version
   * otherwise return false.
   */
  if (latestMajor != currentMajor) return latestMajor > currentMajor;

  /*
   * Compare minor versions.
   * If they differ, return true if latest minor version greater than current minor version
   * otherwise return false.
   */
  if (latestMinor != currentMinor) return latestMinor > currentMinor;

  /*
   * Check patch versions.
   */
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // If we reach here, it means all segments are equal.
  // One final check, if we're on an RC build (contains "-rc"), we should consider the latest version as newer even if
  // the segments are equal, since RC builds are pre-release versions.
  if (strstr(currentVersion, "-rc") != nullptr) {
    return true;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate() {
  InstallCallbackClearer clearCallbacks(this);

  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  processedSize = 0;
  esp_err_t esp_err;
  /* Signal for OtaUpdateActivity */
  render = false;


  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  /* Prefer public DNS — CDN host (release-assets.githubusercontent.com) can fail on ISP DNS. */
  {
    const IPAddress dns1(8, 8, 8, 8);
    const IPAddress dns2(1, 1, 1, 1);
    if (!WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2)) {
      LOG_ERR("OTA", "WiFi.config DNS override failed (continuing)");
    }
  }

  if (!resolveReleaseAssetUrl(otaUrl)) {
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return HTTP_ERROR;
  }


  /*
   * esp_https_ota_begin fails with ESP_ERR_HTTP_CONNECT to GitHub's CDN
   * (release-assets.githubusercontent.com). Use the same NetworkClientSecure +
   * setInsecure() path as HttpDownloader, which already works for HTTPS on device.
   */
  auto secureClient = std::make_unique<NetworkClientSecure>();
  if (!secureClient) {
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return OOM_ERROR;
  }
  secureClient->setInsecure();
  secureClient->setTimeout(120);

  HTTPClient http;
  http.setTimeout(60000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(*secureClient, otaUrl.c_str())) {
    LOG_ERR("OTA", "HTTPClient begin failed");
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return HTTP_ERROR;
  }
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("OTA", "HTTP GET failed: %d", httpCode);
    http.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return HTTP_ERROR;
  }

  {
    const int contentLen = http.getSize();
    if (contentLen > 0) {
      totalSize = static_cast<size_t>(contentLen);
    } else {
      totalSize = otaSize;
    }
  }

  const esp_partition_t* ota_part = esp_ota_get_next_update_partition(nullptr);
  if (!ota_part) {
    LOG_ERR("OTA", "No OTA partition found");
    http.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err = esp_ota_begin(ota_part, totalSize > 0 ? totalSize : OTA_SIZE_UNKNOWN, &ota_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    http.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return INTERNAL_UPDATE_ERROR;
  }

  constexpr size_t kChunk = 1024;
  auto* chunk = static_cast<uint8_t*>(malloc(kChunk));
  if (!chunk) {
    LOG_ERR("OTA", "OTA chunk malloc failed");
    esp_ota_abort(ota_handle);
    http.end();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return OOM_ERROR;
  }

  NetworkClient& stream = http.getStream();
  while (http.connected() && (totalSize == 0 || processedSize < totalSize)) {
    const size_t avail = stream.available();
    if (avail == 0) {
      delay(1);
      if (!http.connected()) {
        break;
      }
      continue;
    }
    const size_t toRead = avail > kChunk ? kChunk : avail;
    const int n = stream.readBytes(chunk, toRead);
    if (n <= 0) {
      break;
    }
    esp_err = esp_ota_write(ota_handle, chunk, static_cast<size_t>(n));
    if (esp_err != ESP_OK) {
      LOG_ERR("OTA", "esp_ota_write failed: %s", esp_err_to_name(esp_err));
      free(chunk);
      esp_ota_abort(ota_handle);
      http.end();
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      return INTERNAL_UPDATE_ERROR;
    }
    processedSize += static_cast<size_t>(n);
    render = true;
    if (installProgressCallback) {
      installProgressCallback(installProgressCallbackCtx);
    }
  }
  free(chunk);
  chunk = nullptr;
  http.end();

  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (processedSize == 0 || (totalSize > 0 && processedSize < totalSize)) {
    LOG_ERR("OTA", "OTA incomplete: read %u / %u", static_cast<unsigned>(processedSize),
            static_cast<unsigned>(totalSize));
    esp_ota_abort(ota_handle);
    return OTA_DOWNLOAD_INCOMPLETE;
  }

  /*
   * Workaround (same idea as crosspoint-halo2-custom): esp_ota_end() -> esp_image_verify()
   * can falsely return ESP_ERR_OTA_VALIDATE_FAILED. Abort the OTA handle (data already on
   * flash), lightly verify the header, then update otadata.
   */
  esp_ota_abort(ota_handle);
  ota_handle = 0;

  {
    uint8_t buf[48];
    esp_err_t read_err = esp_partition_read(ota_part, 0, buf, sizeof(buf));
    if (read_err != ESP_OK) {
      LOG_ERR("OTA", "Partition read failed: %s (0x%x)", esp_err_to_name(read_err),
              static_cast<unsigned>(read_err));
      return OTA_IMAGE_VALIDATE_FAILED;
    }
    if (buf[0] != 0xE9) {
      LOG_ERR("OTA", "Bad image magic: 0x%02X (expected 0xE9)", buf[0]);
      return OTA_IMAGE_VALIDATE_FAILED;
    }
    uint32_t app_magic = 0;
    memcpy(&app_magic, buf + 32, sizeof(app_magic));
    if (app_magic != 0xABCD5432) {
      LOG_ERR("OTA", "Bad app_desc magic: 0x%08lX (expected 0xABCD5432)", static_cast<unsigned long>(app_magic));
      return OTA_IMAGE_VALIDATE_FAILED;
    }
  }

  LOG_INF("OTA", "Firmware header OK, switching boot partition...");

  const esp_partition_t* otadata_part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata_part) {
    LOG_ERR("OTA", "otadata partition not found");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_select_entry_t entry[2];
  esp_partition_read(otadata_part, 0, &entry[0], sizeof(entry[0]));
  esp_partition_read(otadata_part, 0x1000, &entry[1], sizeof(entry[1]));

  const bool valid0 = bootloader_common_ota_select_valid(&entry[0]);
  const bool valid1 = bootloader_common_ota_select_valid(&entry[1]);

  uint32_t max_seq = 0;
  if (valid0) max_seq = entry[0].ota_seq;
  if (valid1 && entry[1].ota_seq > max_seq) max_seq = entry[1].ota_seq;

  const int target_slot = static_cast<int>(ota_part->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0);
  uint32_t new_seq = max_seq + 1;
  if ((new_seq - 1) % 2 != static_cast<uint32_t>(target_slot)) {
    new_seq++;
  }

  esp_ota_select_entry_t new_entry;
  memset(&new_entry, 0xFF, sizeof(new_entry));
  new_entry.ota_seq = new_seq;
  new_entry.ota_state = ESP_OTA_IMG_UNDEFINED;
  new_entry.crc = bootloader_common_ota_select_crc(&new_entry);

  int write_sector;
  if (!valid0) {
    write_sector = 0;
  } else if (!valid1) {
    write_sector = 1;
  } else {
    write_sector = (entry[0].ota_seq <= entry[1].ota_seq) ? 0 : 1;
  }

  const uint32_t write_offset = static_cast<uint32_t>(write_sector) * 0x1000;
  esp_err = esp_partition_erase_range(otadata_part, write_offset, 0x1000);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "otadata erase failed: %s (0x%x)", esp_err_to_name(esp_err), static_cast<unsigned>(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_partition_write(otadata_part, write_offset, &new_entry, sizeof(new_entry));
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "otadata write failed: %s (0x%x)", esp_err_to_name(esp_err), static_cast<unsigned>(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed (ota_seq=%lu slot=%d sector=%d)", static_cast<unsigned long>(new_seq),
          target_slot, write_sector);
  return OK;
}

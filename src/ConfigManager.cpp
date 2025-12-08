#include "ConfigManager.hpp"
#include "ArduinoJson.h"
#include "esp_log.h"
#include "TankManager.hpp"
#include <cstdint>

static const char* TAG = "ConfigManager";

const Recipe Recipe::EMPTY = { 0, "no recipe", std::vector<RecipeIngredient>(), 0, 0, 0.0, 0, false };

ConfigManager::ConfigManager(const char* nvs_namespace) : _namespace(nvs_namespace), _nvs_handle(0) {}



bool ConfigManager::begin()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing and re-initializing.");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS Flash Initialized.");
    return err == ESP_OK;
}

bool ConfigManager::_openNVS()
{
    esp_err_t err = nvs_open(_namespace, NVS_READWRITE, &_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }
    return true;
}

void ConfigManager::_closeNVS()
{
    nvs_close(_nvs_handle);
}

bool ConfigManager::saveWiFiCredentials(const std::string& ssid, const std::string& password)
{
    if (!_openNVS())
        return false;
    nvs_set_str(_nvs_handle, "wifi_ssid", ssid.c_str());
    nvs_set_str(_nvs_handle, "wifi_pass", password.c_str());
    esp_err_t err = nvs_commit(_nvs_handle);
    _closeNVS();
    ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", ssid.c_str());
    return err == ESP_OK;
}

bool ConfigManager::loadWiFiCredentials(std::string& ssid, std::string& password)
{
    if (!_openNVS())
        return false;
    bool success = false;
    size_t required_size;
    if (nvs_get_str(_nvs_handle, "wifi_ssid", NULL, &required_size) == ESP_OK) {
        char* ssid_buf = new char[required_size];
        if (nvs_get_str(_nvs_handle, "wifi_ssid", ssid_buf, &required_size) == ESP_OK) {
            ssid = std::string(ssid_buf);
            if (nvs_get_str(_nvs_handle, "wifi_pass", NULL, &required_size) == ESP_OK) {
                char* pass_buf = new char[required_size];
                if (nvs_get_str(_nvs_handle, "wifi_pass", pass_buf, &required_size) == ESP_OK) {
                    password = std::string(pass_buf);
                    success  = true;
                }
                delete[] pass_buf;
            }
        }
        delete[] ssid_buf;
    }
    _closeNVS();
    return success;
}


bool ConfigManager::saveTimezone(const std::string& tz)
{
    if (!_openNVS())
        return false;
    esp_err_t err = nvs_set_str(_nvs_handle, "timezone", tz.c_str());
    if (err == ESP_OK)
        err = nvs_commit(_nvs_handle);
    _closeNVS();
    return err == ESP_OK;
}

std::string ConfigManager::loadTimezone()
{
    std::string tz = "Etc/UTC"; // Default value
    if (!_openNVS())
        return tz;
    size_t required_size;
    if (nvs_get_str(_nvs_handle, "timezone", NULL, &required_size) == ESP_OK) {
        char* buf = new char[required_size];
        if (nvs_get_str(_nvs_handle, "timezone", buf, &required_size) == ESP_OK) {
            tz = std::string(buf);
        }
        delete[] buf;
    }
    _closeNVS();
    return tz;
}

bool ConfigManager::saveScaleCalibration(float factor, long offset)
{
    if (!_openNVS())
        return false;
    nvs_set_i32(_nvs_handle, "scale_cal_f", (int32_t)(factor * 1000));
    nvs_set_i32(_nvs_handle, "scale_cal_o", offset);
    esp_err_t err = nvs_commit(_nvs_handle);
    _closeNVS();
    return err == ESP_OK;
}

bool ConfigManager::loadScaleCalibration(float& factor, long& offset)
{
    factor = 2280.0f; // Default value
    offset = 0; // Default value
    if (!_openNVS())
        return false;
    int32_t temp_factor;
    if (nvs_get_i32(_nvs_handle, "scale_cal_f", &temp_factor) == ESP_OK) {
        factor = (float)temp_factor / 1000.0f;
    }

    int32_t temp_offset;
    if (nvs_get_i32(_nvs_handle, "scale_cal_o", &temp_offset) == ESP_OK) {
        offset = temp_offset;
    }

    _closeNVS();
    return true;
}

bool ConfigManager::saveHopperCalibration(uint16_t closed_pwm, uint16_t open_pwm)
{
    if (!_openNVS())
        return false;
    nvs_set_u16(_nvs_handle, "hop_closed", closed_pwm);
    nvs_set_u16(_nvs_handle, "hop_open", open_pwm);
    esp_err_t err = nvs_commit(_nvs_handle);
    _closeNVS();
    return err == ESP_OK;
}

bool ConfigManager::loadHopperCalibration(uint16_t& closed_pwm, uint16_t& open_pwm)
{
    // Set to defaults first
    closed_pwm = DEFAULT_HOPPER_CLOSED_PWM;
    open_pwm   = DEFAULT_HOPPER_OPEN_PWM;
    if (!_openNVS())
        return false;

    // Overwrite with saved values if they exist
    nvs_get_u16(_nvs_handle, "hop_closed", &closed_pwm);
    nvs_get_u16(_nvs_handle, "hop_open", &open_pwm);
    _closeNVS();
    return true;
}


bool ConfigManager::saveRecipes(const std::vector<Recipe>& recipes)
{
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    for (const auto& recipe : recipes) {
        JsonObject recipeObj     = array.add<JsonObject>();
        recipeObj["id"]          = recipe.id;
        recipeObj["name"]        = recipe.name;
        recipeObj["dailyWeight"] = recipe.dailyWeight;
        recipeObj["servings"]    = recipe.servings;
        recipeObj["created"]     = recipe.created;
        recipeObj["lastUsed"]    = recipe.lastUsed;

        JsonArray ingredients = recipeObj["ingredients"].to<JsonArray>();
        for (const auto& ing : recipe.ingredients) {
            JsonObject ingObj = ingredients.add<JsonObject>();
            ingObj["tankUid"] = ing.tankUid;
            // Persist the percentage value
            ingObj["percentage"] = ing.percentage;
        }
    }

    std::string jsonString;
    serializeJson(doc, jsonString);

    if (!_openNVS())
        return false;
    esp_err_t err = nvs_set_str(_nvs_handle, "recipes", jsonString.c_str());
    if (err == ESP_OK)
        err = nvs_commit(_nvs_handle);
    _closeNVS();
    ESP_LOGI(TAG, "Saved %d recipes to NVS.", recipes.size());
    return err == ESP_OK;
}

std::vector<Recipe> ConfigManager::loadRecipes()
{
    std::vector<Recipe> recipes;
    if (!_openNVS())
        return recipes;

    size_t required_size;
    if (nvs_get_str(_nvs_handle, "recipes", NULL, &required_size) == ESP_OK) {
        char* buf = new char[required_size];
        if (nvs_get_str(_nvs_handle, "recipes", buf, &required_size) == ESP_OK) {
            JsonDocument doc;
            if (deserializeJson(doc, buf) == DeserializationError::Ok) {
                JsonArray array = doc.as<JsonArray>();
                for (JsonObject recipeObj : array) {
                    Recipe recipe;
                    recipe.id          = recipeObj["id"].as<std::uint64_t>();
                    recipe.name        = recipeObj["name"].as<std::string>();
                    recipe.dailyWeight = recipeObj["dailyWeight"] | 0.0;
                    recipe.servings    = recipeObj["servings"] | 1;
                    recipe.created     = recipeObj["created"] | 0;
                    recipe.lastUsed    = recipeObj["lastUsed"] | 0;

                    for (JsonObject ingObj : recipeObj["ingredients"].as<JsonArray>()) {
                        RecipeIngredient ing;
                        ing.tankUid = ingObj["tankUid"].as<std::uint64_t>();
                        // Load the percentage value
                        ing.percentage = ingObj["percentage"] | 0.0f;
                        recipe.ingredients.push_back(ing);
                    }
                    recipes.push_back(recipe);
                }
            }
        }
        delete[] buf;
    }
    _closeNVS();
    ESP_LOGI(TAG, "Loaded %d recipes from NVS.", recipes.size());
    return recipes;
}

bool ConfigManager::factoryReset()
{
    if (!_openNVS())
        return false;
    esp_err_t err = nvs_erase_all(_nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(_nvs_handle);
    }
    _closeNVS();
    ESP_LOGW(TAG, "NVS namespace '%s' erased.", _namespace);
    return err == ESP_OK;
}

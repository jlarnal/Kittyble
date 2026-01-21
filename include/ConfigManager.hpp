#ifndef CONFIGMANAGER_HPP
#define CONFIGMANAGER_HPP

#include <Arduino.h>
#include <SPIFFS.h>
#include <vector>
#include <string>
#include "nvs_flash.h"
#include "nvs.h"

// Struct for a single ingredient in a recipe
struct RecipeIngredient {
    uint64_t tankUid;
    // Changed to store the ingredient's mix ratio as a percentage.
    float percentage;
};

// Struct for a complete recipe
struct Recipe {
    uint32_t uid;
    std::string name;
    // The ingredients vector now holds percentages.
    std::vector<RecipeIngredient> ingredients;
    long long created;
    long long lastUsed;
    double dailyWeight;
    int servings;
    bool isEnabled;

    static const Recipe EMPTY;
};

class ConfigManager {
public:
    ConfigManager(const char* nvs_namespace);
    bool begin();

    // Configuration Management
    bool saveTimezone(const std::string& tz);
    std::string loadTimezone();
    
    bool saveScaleCalibration(float factor, long offset);
    bool loadScaleCalibration(float& factor, long& offset);

    bool saveWiFiCredentials(const std::string& ssid, const std::string& password);
    bool loadWiFiCredentials(std::string& ssid, std::string& password);

    // Hopper calibration persistence is handled ONLY by ConfigManager
    bool saveHopperCalibration(uint16_t closed_pwm, uint16_t open_pwm);
    bool loadHopperCalibration(uint16_t& closed_pwm, uint16_t& open_pwm);

    // Recipe Management
    bool saveRecipes(const std::vector<Recipe>& recipes);
    std::vector<Recipe> loadRecipes();

 
    // Factory Reset
    bool factoryReset();

private:
    const char* _namespace;
    nvs_handle_t _nvs_handle;

    bool _openNVS();
    void _closeNVS();

    // Recipe file paths (SPIFFS) - triple redundancy
    static constexpr const char* RECIPE_FILE_PRIMARY = "/recipes.json";
    static constexpr const char* RECIPE_FILE_BACKUP1 = "/recipes.bak1.json";
    static constexpr const char* RECIPE_FILE_BACKUP2 = "/recipes.bak2.json";

    // Helper methods for SPIFFS recipe storage
    bool _saveRecipeFile(const char* path, const std::string& jsonContent);
    bool _loadRecipeFile(const char* path, std::vector<Recipe>& recipes);
    uint32_t _computeRecipeCRC(const std::string& jsonStr);
    std::vector<Recipe> _loadRecipesFromNVS_Legacy();
    void _deleteNVSRecipes();
};

#endif // CONFIGMANAGER_HPP

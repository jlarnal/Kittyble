#ifndef RECIPEPROCESSOR_HPP
#define RECIPEPROCESSOR_HPP

#include "DeviceState.hpp"
#include "ConfigManager.hpp"
#include "TankManager.hpp"
#include "HX711Scale.hpp"

#define DISPENSING_LOOP_PERIOD_MS (250)
/**
 * @file RecipeProcessor.hpp
 * @brief Handles the logic for dispensing kibble for recipes or immediate feeds.
 */

class RecipeProcessor {
  public:
    RecipeProcessor(DeviceState& deviceState, SemaphoreHandle_t& mutex, ConfigManager& configManager, TankManager& tankManager,
      HX711Scale& scale);

    void begin();

    // These methods are called by the central feeding task
    bool executeImmediateFeed(const uint64_t tankUid, float targetWeight);
    // Updated to accept the number of servings to dispense, defaulting to 1.
    bool executeRecipeFeed(uint32_t recipeUid, int servings = 1);
    void stopAllFeeding();

    // Recipe management methods (called by WebServer)
    bool addRecipe(const Recipe& recipe);
    bool updateRecipe(const Recipe& recipe);
    bool deleteRecipe(uint32_t recipeUid);
    std::vector<Recipe> getRecipes();
    Recipe getRecipeByUid(uint32_t recipeUid);

    // Provide access to the scale for taring
    HX711Scale& getScale();

  private:
    DeviceState& _deviceState;
    SemaphoreHandle_t& _mutex;
    ConfigManager& _configManager;
    TankManager& _tankManager;
    HX711Scale& _scale;

    std::vector<Recipe> _recipes;

    void _loadRecipesFromNVS();
    void _saveRecipesToNVS();
    bool _dispenseIngredient(const uint64_t tankUid, float targetWeight);
};

#endif // RECIPEPROCESSOR_HPP

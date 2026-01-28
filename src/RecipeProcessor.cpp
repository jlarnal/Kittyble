#include "RecipeProcessor.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cmath>

static const char* TAG = "RecipeProcessor";

RecipeProcessor::RecipeProcessor(
  DeviceState& deviceState, SemaphoreHandle_t& mutex, ConfigManager& configManager, TankManager& tankManager, HX711Scale& scale)
    : _deviceState(deviceState), _mutex(mutex), _configManager(configManager), _tankManager(tankManager), _scale(scale)
{
    _ctx.reset();
}

void RecipeProcessor::begin()
{
    _loadRecipesFromNVS();
    ESP_LOGI(TAG, "Loaded %d recipes from NVS.", _recipes.size());
}

HX711Scale& RecipeProcessor::getScale()
{
    return _scale;
}

// ============================================================================
// Public Feed Methods
// ============================================================================

bool RecipeProcessor::executeImmediateFeed(const uint64_t tankUid, float targetWeight)
{
    if (tankUid == 0) {
        ESP_LOGE(TAG, "Immediate feed failed: No tank UID provided.");
        if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _deviceState.lastEvent = DeviceEvent_e::DEVEVENT_NO_TANK_SPECIFIED;
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    // Validate tank exists
    int8_t servoId = _tankManager.getBusOfTank(tankUid);
    if (servoId < 0) {
        ESP_LOGE(TAG, "Immediate feed failed: Tank 0x%016llx not found.", tankUid);
        if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _deviceState.lastEvent = DeviceEvent_e::DEVEVENT_TANK_NOT_FOUND;
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    ESP_LOGI(TAG, "Starting immediate feed of %.2fg from tank 0x%016llx", targetWeight, tankUid);

    // Create a single-ingredient list for immediate feed
    std::vector<RecipeIngredient> ingredients;
    RecipeIngredient ingredient;
    ingredient.tankUid = tankUid;
    ingredient.percentage = 100.0f;
    ingredients.push_back(ingredient);

    // Prepare context (recipeUid = 0 for immediate feed, servings = 1)
    _prepareDispensingContext(0, ingredients, targetWeight, 1);

    // Execute dispensing cycles until complete
    bool success = true;
    while (_hasMoreToDispense() && success) {
        if (_checkEmergencyStop()) {
            _handleError(DispensingError::ERR_EMERGENCY_STOP);
            success = false;
            break;
        }
        success = _executeCycle();
        if (success) {
            vTaskDelay(pdMS_TO_TICKS(POST_BATCH_DELAY_MS));
        }
    }

    // Final purge to release last batch, then close hopper
    if (success) {
        ESP_LOGI(TAG, "Final purge to release last batch.");
        _purgeHopper();
        ESP_LOGI(TAG, "Closing hopper to idle position.");
        _tankManager.closeHopper();
    }

    // Log the immediate feeding event
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        FeedingHistoryEntry entry(time(nullptr), "immediate", 0, success, _ctx.dispensedGrams, "Immediate Feed");
        _deviceState.feedingHistory.push_back(entry);
        xSemaphoreGive(_mutex);
    }

    ESP_LOGI(TAG, "Immediate feed %s. Dispensed %.2fg of %.2fg target.",
             success ? "completed" : "failed", _ctx.dispensedGrams, targetWeight);

    return success;
}

bool RecipeProcessor::executeRecipeFeed(uint32_t recipeUid, int servings)
{
    auto it = std::find_if(_recipes.begin(), _recipes.end(), [recipeUid](const Recipe& r) { return r.uid == recipeUid; });

    if (it == _recipes.end()) {
        ESP_LOGE(TAG, "Recipe feed failed: Recipe with UID %u not found.", recipeUid);
        if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _deviceState.lastEvent = DeviceEvent_e::DEVEVENT_RECIPE_NOT_FOUND;
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    Recipe& recipe = *it;

    // Validate servings: must be >= 1, default to 3 if invalid
    if (servings < 1) {
        ESP_LOGW(TAG, "Invalid servings %d, defaulting to %d.", servings, DEFAULT_SERVINGS);
        servings = DEFAULT_SERVINGS;
    }

    // Validate recipe.servings for portion calculation
    int recipeServings = recipe.servings;
    if (recipeServings <= 0) {
        ESP_LOGW(TAG, "Recipe '%s' has invalid servings %d, defaulting to %d.",
                 recipe.name.c_str(), recipeServings, DEFAULT_SERVINGS);
        recipeServings = DEFAULT_SERVINGS;
    }

    // Calculate total target: (dailyWeight / recipe.servings) * requested servings
    float singleServingWeight = recipe.dailyWeight / (float)recipeServings;
    float totalTargetGrams = singleServingWeight * servings;

    ESP_LOGI(TAG, "Executing recipe '%s' for %d serving(s). Single serving: %.2fg, Total target: %.2fg",
             recipe.name.c_str(), servings, singleServingWeight, totalTargetGrams);

    // Prepare dispensing context
    _prepareDispensingContext(recipeUid, recipe.ingredients, totalTargetGrams, servings);

    // Execute dispensing cycles until complete
    bool success = true;
    while (_hasMoreToDispense() && success) {
        if (_checkEmergencyStop()) {
            _handleError(DispensingError::ERR_EMERGENCY_STOP);
            success = false;
            break;
        }
        success = _executeCycle();
        if (success && _hasMoreToDispense()) {
            vTaskDelay(pdMS_TO_TICKS(POST_BATCH_DELAY_MS));
        }
    }

    // Final purge to release last batch, then close hopper
    if (success) {
        ESP_LOGI(TAG, "Final purge to release last batch.");
        _purgeHopper();
        ESP_LOGI(TAG, "Closing hopper to idle position.");
        _tankManager.closeHopper();
    }

    if (success) {
        ESP_LOGI(TAG, "Recipe '%s' completed successfully.", recipe.name.c_str());
        recipe.lastUsed = time(nullptr);
        _saveRecipesToNVS();
    }

    // Log the feeding event
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        FeedingHistoryEntry entry(time(nullptr), "recipe", recipe.uid, success, _ctx.dispensedGrams, recipe.name);
        _deviceState.feedingHistory.push_back(entry);
        xSemaphoreGive(_mutex);
    }

    ESP_LOGI(TAG, "Recipe feed %s. Dispensed %.2fg of %.2fg target.",
             success ? "completed" : "failed", _ctx.dispensedGrams, totalTargetGrams);

    return success;
}

void RecipeProcessor::stopAllFeeding()
{
    ESP_LOGW(TAG, "Stopping all feeding - closing hopper.");
    _tankManager.closeHopper();
    vTaskDelay(pdMS_TO_TICKS(300)); // Allow hopper to physically close
    _tankManager.stopAllServos();
    _ctx.phase = DispensingPhase::PHASE_IDLE;
}

// ============================================================================
// Context Management
// ============================================================================

void RecipeProcessor::_prepareDispensingContext(uint32_t recipeUid,
                                                 const std::vector<RecipeIngredient>& ingredients,
                                                 float totalGrams,
                                                 int servings)
{
    _ctx.reset();
    _ctx.recipeUid = recipeUid;
    _ctx.ingredients = ingredients;
    _ctx.totalTargetGrams = totalGrams;
    _ctx.servings = servings;

    // Initialize per-ingredient remaining grams based on percentage
    size_t numIngredients = std::min(ingredients.size(), (size_t)MAX_INGREDIENTS);
    for (size_t i = 0; i < numIngredients; i++) {
        _ctx.ingredientRemainingGrams[i] = totalGrams * (ingredients[i].percentage / 100.0f);
        ESP_LOGD(TAG, "Ingredient %zu (tank 0x%016llx): %.2fg (%.1f%%)",
                 i, ingredients[i].tankUid, _ctx.ingredientRemainingGrams[i], ingredients[i].percentage);
    }

    // Power on servos for the operation
    _tankManager.setServoPower(true);
    vTaskDelay(pdMS_TO_TICKS(200)); // Wait for servo power stabilization
}

bool RecipeProcessor::_hasMoreToDispense() const
{
    return _ctx.dispensedGrams < (_ctx.totalTargetGrams - 0.5f); // 0.5g tolerance
}

// ============================================================================
// Main Cycle Execution
// ============================================================================

bool RecipeProcessor::_executeCycle()
{
    ESP_LOGI(TAG, "Starting dispense cycle. Dispensed so far: %.2fg / %.2fg",
             _ctx.dispensedGrams, _ctx.totalTargetGrams);

    // Phase 1: Purge
    if (!_purgeHopper()) {
        return false;
    }

    // Phase 2: Close & Tare
    if (!_closeAndTareHopper()) {
        return false;
    }

    // Phase 3: Dispense batch
    if (!_dispenseBatch()) {
        return false;
    }

    return true;
}

// ============================================================================
// Phase 1: Purge
// ============================================================================

bool RecipeProcessor::_purgeHopper()
{
    ESP_LOGI(TAG, "PHASE: Purge - Opening hopper");
    _ctx.phase = DispensingPhase::PHASE_PURGE_OPEN;
    _ctx.phaseStartTick = xTaskGetTickCount();

    // Open hopper
    auto result = _tankManager.openHopper();
    if (result != PCA9685::I2C_Result_e::I2C_Ok) {
        ESP_LOGE(TAG, "Failed to open hopper: I2C error");
        _handleError(DispensingError::ERR_SERVO_TIMEOUT);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Brief delay for servo to move

    // Check emergency stop
    if (_checkEmergencyStop()) {
        _handleError(DispensingError::ERR_EMERGENCY_STOP);
        return false;
    }

    // Execute wiggle
    if (!_executeWiggle()) {
        return false;
    }

    // Settle phase - wait for stray kibbles to fall
    ESP_LOGI(TAG, "PHASE: Purge settle - waiting %dms", HOPPER_PURGE_DELAY_MS);
    _ctx.phase = DispensingPhase::PHASE_PURGE_SETTLE;
    _ctx.phaseStartTick = xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(HOPPER_PURGE_DELAY_MS));

    return true;
}

bool RecipeProcessor::_executeWiggle()
{
    ESP_LOGI(TAG, "PHASE: Purge wiggle - %d cycles", WIGGLE_CYCLE_COUNT);
    _ctx.phase = DispensingPhase::PHASE_PURGE_WIGGLE;
    _ctx.phaseStartTick = xTaskGetTickCount();
    _ctx.wiggleCount = 0;

    uint16_t openPwm = _tankManager.getHopperOpenPwm();

    for (uint8_t i = 0; i < WIGGLE_CYCLE_COUNT; i++) {
        if (_checkEmergencyStop()) {
            _handleError(DispensingError::ERR_EMERGENCY_STOP);
            return false;
        }

        // Wiggle one direction
        _tankManager.setServoPWM(HOPPER_SERVO_INDEX, openPwm + WIGGLE_AMPLITUDE_PWM);
        vTaskDelay(pdMS_TO_TICKS(WIGGLE_HALF_PERIOD_MS));

        // Wiggle other direction
        _tankManager.setServoPWM(HOPPER_SERVO_INDEX, openPwm - WIGGLE_AMPLITUDE_PWM);
        vTaskDelay(pdMS_TO_TICKS(WIGGLE_HALF_PERIOD_MS));

        _ctx.wiggleCount++;
    }

    // Return to center (open position)
    _tankManager.setServoPWM(HOPPER_SERVO_INDEX, openPwm);
    vTaskDelay(pdMS_TO_TICKS(100));

    return true;
}

// ============================================================================
// Phase 2: Close & Tare
// ============================================================================

bool RecipeProcessor::_closeAndTareHopper()
{
    ESP_LOGI(TAG, "PHASE: Close hopper with spike detection");
    _ctx.phase = DispensingPhase::PHASE_CLOSE_MOVING;
    _ctx.phaseStartTick = xTaskGetTickCount();
    _ctx.closeAttempts = 0;

    // Record pre-close weight
    _ctx.preCloseWeight = _scale.getWeight();
    if (std::isnan(_ctx.preCloseWeight)) {
        ESP_LOGE(TAG, "Scale unresponsive before close");
        _handleError(DispensingError::ERR_SCALE_UNRESPONSIVE);
        return false;
    }

    // Attempt spike detection
    bool spikeDetected = _detectCloseSpike();

    if (!spikeDetected) {
        ESP_LOGW(TAG, "Close spike not detected after %d attempts, using default close PWM",
                 _ctx.closeAttempts);
        // Fall back to default close position
        _tankManager.closeHopper();
        _ctx.learnedClosePwm = _tankManager.getHopperClosedPwm();
    }

    // Wait for things to settle
    vTaskDelay(pdMS_TO_TICKS(TARE_SETTLE_MS));

    // Tare the scale
    ESP_LOGI(TAG, "PHASE: Tare scale");
    _ctx.phase = DispensingPhase::PHASE_TARE;
    _ctx.phaseStartTick = xTaskGetTickCount();
    _scale.tare();
    vTaskDelay(pdMS_TO_TICKS(TARE_SETTLE_MS));

    float postTareWeight = _scale.getWeight();
    if (std::isnan(postTareWeight)) {
        ESP_LOGE(TAG, "Scale unresponsive after tare");
        _handleError(DispensingError::ERR_SCALE_UNRESPONSIVE);
        return false;
    }

    ESP_LOGI(TAG, "Tare complete. Post-tare weight: %.2fg", postTareWeight);
    return true;
}

bool RecipeProcessor::_detectCloseSpike()
{
    _ctx.phase = DispensingPhase::PHASE_CLOSE_DETECT_SPIKE;
    _ctx.phaseStartTick = xTaskGetTickCount();

    uint16_t openPwm = _tankManager.getHopperOpenPwm();
    uint16_t closedPwm = _tankManager.getHopperClosedPwm();

    // Determine direction: open->closed
    int16_t step = (closedPwm > openPwm) ? CLOSE_STEP_PWM : -CLOSE_STEP_PWM;
    uint16_t currentPwm = openPwm;

    float baselineWeight = _scale.getWeight();
    if (std::isnan(baselineWeight)) {
        return false;
    }

    ESP_LOGD(TAG, "Starting close detection from PWM %d to %d (step %d)", openPwm, closedPwm, step);

    while (_ctx.closeAttempts < CLOSE_MAX_ATTEMPTS) {
        if (_checkEmergencyStop()) {
            _handleError(DispensingError::ERR_EMERGENCY_STOP);
            return false;
        }

        // Step the PWM
        currentPwm += step;

        // Check if we've passed the target
        if ((step > 0 && currentPwm >= closedPwm) || (step < 0 && currentPwm <= closedPwm)) {
            currentPwm = closedPwm;
        }

        _tankManager.setServoPWM(HOPPER_SERVO_INDEX, currentPwm);
        vTaskDelay(pdMS_TO_TICKS(CLOSE_STEP_DELAY_MS));

        // Check for weight spike
        float currentWeight = _scale.getWeight();
        if (std::isnan(currentWeight)) {
            ESP_LOGW(TAG, "Scale read NaN during close detection");
            continue;
        }

        float weightChange = currentWeight - baselineWeight;
        _ctx.closeAttempts++;

        if (weightChange >= CLOSE_WEIGHT_SPIKE_GRAMS) {
            ESP_LOGI(TAG, "Spike detected! Weight change: %.2fg at PWM %d (attempt %d)",
                     weightChange, currentPwm, _ctx.closeAttempts);

            // Back off slightly
            _ctx.phase = DispensingPhase::PHASE_CLOSE_BACKOFF;
            uint16_t backoffPwm = currentPwm - (step > 0 ? CLOSE_BACKOFF_PWM : -CLOSE_BACKOFF_PWM);
            _tankManager.setServoPWM(HOPPER_SERVO_INDEX, backoffPwm);
            _ctx.learnedClosePwm = backoffPwm;
            _ctx.closeCalibrated = true;
            vTaskDelay(pdMS_TO_TICKS(100));

            return true;
        }

        // If we've reached the target without detecting spike
        if (currentPwm == closedPwm) {
            break;
        }
    }

    return false;
}

// ============================================================================
// Phase 3: Dispense
// ============================================================================

bool RecipeProcessor::_dispenseBatch()
{
    ESP_LOGI(TAG, "PHASE: Dispense batch");
    _ctx.phase = DispensingPhase::PHASE_DISPENSE_AUGER;
    _ctx.phaseStartTick = xTaskGetTickCount();

    // Calculate batch target
    float batchTarget = _calculateBatchTarget();
    _ctx.currentBatchTargetGrams = batchTarget;
    _ctx.currentBatchDispensedGrams = 0.0f;

    ESP_LOGI(TAG, "Batch target: %.2fg", batchTarget);

    if (batchTarget < 0.5f) {
        ESP_LOGW(TAG, "Batch target too small (%.2fg), skipping", batchTarget);
        return true;
    }

    // Dispense from each ingredient proportionally
    size_t numIngredients = std::min(_ctx.ingredients.size(), (size_t)MAX_INGREDIENTS);

    for (size_t i = 0; i < numIngredients; i++) {
        if (_checkEmergencyStop()) {
            _handleError(DispensingError::ERR_EMERGENCY_STOP);
            return false;
        }

        float ingredientRemaining = _ctx.ingredientRemainingGrams[i];
        if (ingredientRemaining < 0.5f) {
            continue; // Skip depleted ingredients
        }

        // Calculate this ingredient's portion of the batch
        float percentage = _ctx.ingredients[i].percentage / 100.0f;
        float ingredientTarget = batchTarget * percentage;
        ingredientTarget = std::min(ingredientTarget, ingredientRemaining);

        if (ingredientTarget < 0.5f) {
            continue;
        }

        ESP_LOGI(TAG, "Dispensing %.2fg from ingredient %zu (tank 0x%016llx)",
                 ingredientTarget, i, _ctx.ingredients[i].tankUid);

        float dispensed = 0.0f;
        bool success = _runAugerForIngredient(_ctx.ingredients[i].tankUid, ingredientTarget, dispensed);

        // Update tracking regardless of success
        _ctx.ingredientRemainingGrams[i] -= dispensed;
        _ctx.currentBatchDispensedGrams += dispensed;
        _ctx.dispensedGrams += dispensed;

        if (!success) {
            // Log but don't fail - try other ingredients
            ESP_LOGW(TAG, "Ingredient %zu dispense incomplete: %.2fg of %.2fg",
                     i, dispensed, ingredientTarget);
        }
    }

    // Settling phase
    ESP_LOGI(TAG, "PHASE: Dispense settle - waiting %dms", DISPENSE_SETTLE_MS);
    _ctx.phase = DispensingPhase::PHASE_DISPENSE_SETTLE;
    vTaskDelay(pdMS_TO_TICKS(DISPENSE_SETTLE_MS));

    ESP_LOGI(TAG, "Batch complete: dispensed %.2fg (target %.2fg). Total: %.2fg / %.2fg",
             _ctx.currentBatchDispensedGrams, _ctx.currentBatchTargetGrams,
             _ctx.dispensedGrams, _ctx.totalTargetGrams);

    return true;
}

float RecipeProcessor::_calculateBatchTarget()
{
    // Remaining to dispense
    float remaining = _ctx.totalTargetGrams - _ctx.dispensedGrams;

    // Calculate max hopper capacity based on densest ingredient
    float minDensityGramsPerLiter = 0.0f;
    bool firstValid = true;

    for (size_t i = 0; i < std::min(_ctx.ingredients.size(), (size_t)MAX_INGREDIENTS); i++) {
        if (_ctx.ingredientRemainingGrams[i] < 0.5f) {
            continue;
        }
        float density = _getTankDensityGramsPerLiter(_ctx.ingredients[i].tankUid);
        if (density > 0.0f) {
            if (firstValid || density < minDensityGramsPerLiter) {
                minDensityGramsPerLiter = density;
                firstValid = false;
            }
        }
    }

    // Default density if none found
    if (minDensityGramsPerLiter <= 0.0f) {
        minDensityGramsPerLiter = 500.0f; // Reasonable default for kibble
    }

    // Max grams = MAX_HOPPER_VOLUME_LITERS * density
    float maxHopperGrams = MAX_HOPPER_VOLUME_LITERS * minDensityGramsPerLiter;

    ESP_LOGD(TAG, "Batch calc: remaining=%.2fg, density=%.1f g/L, maxHopper=%.2fg",
             remaining, minDensityGramsPerLiter, maxHopperGrams);

    return std::min(remaining, maxHopperGrams);
}

bool RecipeProcessor::_runAugerForIngredient(uint64_t tankUid, float targetGrams, float& dispensedOut)
{
    dispensedOut = 0.0f;

    int8_t servoId = _tankManager.getBusOfTank(tankUid);
    if (servoId < 0) {
        ESP_LOGE(TAG, "Auger failed: tank 0x%016llx not found", tankUid);
        if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _deviceState.lastEvent = DeviceEvent_e::DEVEVENT_TANK_NOT_FOUND;
            xSemaphoreGive(_mutex);
        }
        return false;
    }

    float initialWeight = _scale.getWeight();
    if (std::isnan(initialWeight)) {
        ESP_LOGE(TAG, "Scale unresponsive before auger run");
        _handleError(DispensingError::ERR_SCALE_UNRESPONSIVE);
        return false;
    }

    // Start auger at full speed
    _tankManager.setContinuousServo(servoId, AUGER_FULL_SPEED);

    TickType_t startTime = xTaskGetTickCount();
    float prevWeight = initialWeight;
    TickType_t lastWeightChangeTime = startTime;

    while (dispensedOut < targetGrams) {
        if (_checkEmergencyStop()) {
            _tankManager.setContinuousServo(servoId, 0.0f);
            _handleError(DispensingError::ERR_EMERGENCY_STOP);
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(DISPENSING_LOOP_PERIOD_MS));

        float currentWeight = _scale.getWeight();
        if (std::isnan(currentWeight)) {
            ESP_LOGW(TAG, "Scale read NaN during auger");
            continue;
        }

        dispensedOut = currentWeight - initialWeight;

        // Check for weight change (stall detection)
        if (abs(currentWeight - prevWeight) >= _deviceState.Settings.getDispensingWeightChangeThreshold()) {
            lastWeightChangeTime = xTaskGetTickCount();
        }
        prevWeight = currentWeight;

        // Timeout check
        uint32_t timeoutMs = _deviceState.Settings.getDispensingNoWeightChangeTimeout_ms();
        if ((xTaskGetTickCount() - lastWeightChangeTime) > pdMS_TO_TICKS(timeoutMs)) {
            ESP_LOGW(TAG, "Auger timeout for tank 0x%016llx - tank may be empty", tankUid);
            _tankManager.setContinuousServo(servoId, 0.0f);
            if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
                _deviceState.lastEvent = DeviceEvent_e::DEVEVENT_TANK_EMPTY;
                xSemaphoreGive(_mutex);
            }
            return false;
        }

        // Slow down when approaching target
        float remaining = targetGrams - dispensedOut;
        if (remaining < AUGER_SLOW_THRESHOLD_GRAMS) {
            _tankManager.setContinuousServo(servoId, AUGER_SLOW_SPEED);
        }
    }

    // Stop auger
    _tankManager.setContinuousServo(servoId, 0.0f);

    ESP_LOGI(TAG, "Auger complete: dispensed %.2fg (target %.2fg) from tank 0x%016llx",
             dispensedOut, targetGrams, tankUid);

    return true;
}

// ============================================================================
// Error Handling & Utilities
// ============================================================================

bool RecipeProcessor::_checkEmergencyStop()
{
    bool stopped = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        stopped = (_deviceState.feedCommand.type == FeedCommandType::EMERGENCY_STOP);
        xSemaphoreGive(_mutex);
    }
    return stopped;
}

void RecipeProcessor::_handleError(DispensingError error)
{
    _ctx.error = error;
    _ctx.phase = DispensingPhase::PHASE_ERROR;

    const char* errorStr = "UNKNOWN";
    DeviceEvent_e event = DeviceEvent_e::DEVEVENT_NONE;

    switch (error) {
        case DispensingError::ERR_CLOSE_DETECTION_FAILED:
            errorStr = "CLOSE_DETECTION_FAILED";
            break;
        case DispensingError::ERR_TANK_EMPTY:
            errorStr = "TANK_EMPTY";
            event = DeviceEvent_e::DEVEVENT_TANK_EMPTY;
            break;
        case DispensingError::ERR_SCALE_UNRESPONSIVE:
            errorStr = "SCALE_UNRESPONSIVE";
            break;
        case DispensingError::ERR_SERVO_TIMEOUT:
            errorStr = "SERVO_TIMEOUT";
            break;
        case DispensingError::ERR_EMERGENCY_STOP:
            errorStr = "EMERGENCY_STOP";
            event = DeviceEvent_e::DEVEVENT_USER_STOPPED;
            break;
        case DispensingError::ERR_DISPENSE_TIMEOUT:
            errorStr = "DISPENSE_TIMEOUT";
            event = DeviceEvent_e::DEVEVENT_DISPENSE_TIMEOUT;
            break;
        default:
            break;
    }

    ESP_LOGE(TAG, "Dispensing error: %s", errorStr);

    if (event != DeviceEvent_e::DEVEVENT_NONE) {
        if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _deviceState.lastEvent = event;
            xSemaphoreGive(_mutex);
        }
    }

    stopAllFeeding();
}

float RecipeProcessor::_getTankDensityGramsPerLiter(uint64_t tankUid)
{
    TankInfo* tank = _tankManager.getKnownTankOfUis(tankUid);
    if (tank == nullptr) {
        return 0.0f;
    }
    // kibbleDensity is stored as kg/L, convert to g/L
    return tank->kibbleDensity * 1000.0f;
}

// ============================================================================
// Recipe Management
// ============================================================================

void RecipeProcessor::_loadRecipesFromNVS()
{
    _recipes = _configManager.loadRecipes();
    _deviceState.storedRecipes = _recipes;
}

void RecipeProcessor::_saveRecipesToNVS()
{
    _configManager.saveRecipes(_recipes);
}

bool RecipeProcessor::addRecipe(const Recipe& recipe)
{
    uint32_t maxUid = 0;
    for (const auto& r : _recipes) {
        if (r.uid > maxUid)
            maxUid = r.uid;
    }
    Recipe newRecipe   = recipe;
    newRecipe.uid      = maxUid + 1;
    newRecipe.created  = time(nullptr);
    newRecipe.lastUsed = 0;
    _recipes.push_back(newRecipe);
    _saveRecipesToNVS();
    _deviceState.storedRecipes = _recipes;
    ESP_LOGI(TAG, "Added new recipe '%s' with UID %u", newRecipe.name.c_str(), newRecipe.uid);
    return true;
}

bool RecipeProcessor::updateRecipe(const Recipe& recipe)
{
    for (auto& r : _recipes) {
        if (r.uid == recipe.uid) {
            r.name        = recipe.name;
            r.ingredients = recipe.ingredients;
            r.dailyWeight = recipe.dailyWeight;
            r.servings    = recipe.servings;
            r.lastUsed    = time(nullptr);
            _saveRecipesToNVS();
            _deviceState.storedRecipes = _recipes;
            ESP_LOGI(TAG, "Updated recipe '%s' (UID %u)", r.name.c_str(), r.uid);
            return true;
        }
    }
    ESP_LOGW(TAG, "Could not find recipe with UID %u to update.", recipe.uid);
    return false;
}

bool RecipeProcessor::deleteRecipe(uint32_t recipeUid)
{
    auto it = std::remove_if(_recipes.begin(), _recipes.end(), [recipeUid](const Recipe& r) { return r.uid == recipeUid; });

    if (it != _recipes.end()) {
        _recipes.erase(it, _recipes.end());
        _saveRecipesToNVS();
        _deviceState.storedRecipes = _recipes;
        ESP_LOGI(TAG, "Deleted recipe with UID %u", recipeUid);
        return true;
    }
    ESP_LOGW(TAG, "Could not find recipe with UID %u to delete.", recipeUid);
    return false;
}

std::vector<Recipe> RecipeProcessor::getRecipes()
{
    return _recipes;
}

Recipe RecipeProcessor::getRecipeByUid(uint32_t recipeUid)
{
    for (const auto& r : _recipes) {
        if (r.uid == recipeUid) {
            return r;
        }
    }
    return { 0, "Not Found", {}, 0, 0, 0, 0, false };
}

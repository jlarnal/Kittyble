#ifndef RECIPEPROCESSOR_HPP
#define RECIPEPROCESSOR_HPP

#include "DeviceState.hpp"
#include "ConfigManager.hpp"
#include "TankManager.hpp"
#include "HX711Scale.hpp"

#define DISPENSING_LOOP_PERIOD_MS (250)

// ============================================================================
// Hopper Constants
// ============================================================================
#define MAX_HOPPER_VOLUME_LITERS     (0.01f)
#define HOPPER_PURGE_DELAY_MS        (2000)

// ============================================================================
// Wiggle Constants
// ============================================================================
#define WIGGLE_AMPLITUDE_PWM         (150)
#define WIGGLE_HALF_PERIOD_MS        (200)
#define WIGGLE_CYCLE_COUNT           (4)

// ============================================================================
// Close Detection Constants
// ============================================================================
#define CLOSE_STEP_PWM               (25)
#define CLOSE_STEP_DELAY_MS          (100)
#define CLOSE_WEIGHT_SPIKE_GRAMS     (3.0f)
#define CLOSE_BACKOFF_PWM            (50)
#define CLOSE_MAX_ATTEMPTS           (60)

// ============================================================================
// Settling/Timing Constants
// ============================================================================
#define DISPENSE_SETTLE_MS           (500)
#define TARE_SETTLE_MS               (300)
#define POST_BATCH_DELAY_MS          (200)

// ============================================================================
// Auger Constants
// ============================================================================
#define AUGER_SLOW_THRESHOLD_GRAMS   (2.0f)
#define AUGER_FULL_SPEED             (1.0f)
#define AUGER_SLOW_SPEED             (0.2f)

// ============================================================================
// Default Servings
// ============================================================================
#define DEFAULT_SERVINGS             (3)
#define MAX_INGREDIENTS              (6)

/**
 * @file RecipeProcessor.hpp
 * @brief Handles the logic for dispensing kibble for recipes or immediate feeds.
 *
 * The dispensing routine follows a three-phase cycle:
 * 1. PURGE - Open hopper, wiggle to dislodge stuck kibbles, wait for settling
 * 2. CLOSE & TARE - Close hopper with weight spike detection, tare scale
 * 3. DISPENSE - Fill hopper in batches, mixing ingredients proportionally
 */

/**
 * @enum DispensingPhase
 * @brief States for the dispensing state machine
 */
enum class DispensingPhase : uint8_t {
    PHASE_IDLE,              ///< No dispensing operation in progress
    PHASE_PURGE_OPEN,        ///< Opening the hopper trapdoor
    PHASE_PURGE_WIGGLE,      ///< Wiggling to dislodge stuck kibbles
    PHASE_PURGE_SETTLE,      ///< Waiting for kibbles to fall through
    PHASE_CLOSE_MOVING,      ///< Gradually closing hopper
    PHASE_CLOSE_DETECT_SPIKE,///< Monitoring scale for weight spike
    PHASE_CLOSE_BACKOFF,     ///< Backing off after spike detection
    PHASE_TARE,              ///< Taring the scale
    PHASE_DISPENSE_AUGER,    ///< Running auger to dispense kibble
    PHASE_DISPENSE_SETTLE,   ///< Waiting for dispensed kibbles to settle
    PHASE_COMPLETE,          ///< Dispensing cycle completed successfully
    PHASE_ERROR              ///< Error occurred during dispensing
};

/**
 * @enum DispensingError
 * @brief Error codes for dispensing operations
 */
enum class DispensingError : uint8_t {
    ERR_NONE,                    ///< No error
    ERR_CLOSE_DETECTION_FAILED,  ///< Failed to detect hopper close via weight spike
    ERR_TANK_EMPTY,              ///< Tank ran out of kibble during dispense
    ERR_SCALE_UNRESPONSIVE,      ///< Scale returned NaN or failed to respond
    ERR_SERVO_TIMEOUT,           ///< Servo operation timed out
    ERR_EMERGENCY_STOP,          ///< Emergency stop was triggered
    ERR_DISPENSE_TIMEOUT         ///< Dispense operation timed out (no weight change)
};

/**
 * @struct DispensingContext
 * @brief Holds all state for a dispensing operation
 */
struct DispensingContext {
    // Recipe/feed identification
    uint32_t recipeUid;                          ///< Recipe UID (0 for immediate feed)
    std::vector<RecipeIngredient> ingredients;   ///< List of ingredients to dispense
    float totalTargetGrams;                      ///< Total target weight for entire operation
    float dispensedGrams;                        ///< Total weight dispensed so far
    int servings;                                ///< Number of servings (validated >= 1)

    // Current batch tracking
    float currentBatchTargetGrams;               ///< Target weight for current batch
    float currentBatchDispensedGrams;            ///< Weight dispensed in current batch
    size_t currentIngredientIndex;               ///< Index of ingredient being dispensed
    float ingredientRemainingGrams[MAX_INGREDIENTS]; ///< Remaining grams per ingredient

    // Hopper close calibration
    uint16_t learnedClosePwm;                    ///< PWM value that closes hopper (learned)
    bool closeCalibrated;                        ///< Whether close position has been learned

    // State machine
    DispensingPhase phase;                       ///< Current dispensing phase
    DispensingError error;                       ///< Error code if phase == PHASE_ERROR
    TickType_t phaseStartTick;                   ///< Tick count when current phase started

    // Phase-specific counters
    uint8_t wiggleCount;                         ///< Number of wiggle cycles completed
    uint8_t closeAttempts;                       ///< Number of close detection steps taken
    float preCloseWeight;                        ///< Weight reading before starting close

    /**
     * @brief Reset context to initial state
     */
    void reset() {
        recipeUid = 0;
        ingredients.clear();
        totalTargetGrams = 0.0f;
        dispensedGrams = 0.0f;
        servings = DEFAULT_SERVINGS;

        currentBatchTargetGrams = 0.0f;
        currentBatchDispensedGrams = 0.0f;
        currentIngredientIndex = 0;
        for (int i = 0; i < MAX_INGREDIENTS; i++) {
            ingredientRemainingGrams[i] = 0.0f;
        }

        learnedClosePwm = 0;
        closeCalibrated = false;

        phase = DispensingPhase::PHASE_IDLE;
        error = DispensingError::ERR_NONE;
        phaseStartTick = 0;

        wiggleCount = 0;
        closeAttempts = 0;
        preCloseWeight = 0.0f;
    }
};

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
    DispensingContext _ctx;

    // Recipe persistence
    void _loadRecipesFromNVS();
    void _saveRecipesToNVS();

    // ========================================================================
    // Three-Phase Dispensing Cycle Methods
    // ========================================================================

    /**
     * @brief Execute the complete purge-close-dispense cycle
     * @return true if cycle completed successfully
     */
    bool _executeCycle();

    /**
     * @brief Prepare the dispensing context for a recipe or immediate feed
     * @param recipeUid Recipe UID (0 for immediate feed)
     * @param ingredients List of ingredients to dispense
     * @param totalGrams Total weight to dispense
     * @param servings Number of servings
     */
    void _prepareDispensingContext(uint32_t recipeUid,
                                    const std::vector<RecipeIngredient>& ingredients,
                                    float totalGrams,
                                    int servings);

    /**
     * @brief Check if there's more kibble to dispense
     * @return true if dispensedGrams < totalTargetGrams
     */
    bool _hasMoreToDispense() const;

    // --- Phase 1: Purge ---

    /**
     * @brief Execute full purge sequence: open, wiggle, settle
     * @return true if purge completed successfully
     */
    bool _purgeHopper();

    /**
     * @brief Execute wiggle oscillations around open position
     * @return true if wiggle completed
     */
    bool _executeWiggle();

    // --- Phase 2: Close & Tare ---

    /**
     * @brief Gradually close hopper with spike detection, then tare
     * @return true if close and tare completed successfully
     */
    bool _closeAndTareHopper();

    /**
     * @brief Step PWM and monitor scale for weight spike
     * @return true if spike detected, false if max attempts reached
     */
    bool _detectCloseSpike();

    // --- Phase 3: Dispense ---

    /**
     * @brief Dispense one batch (up to MAX_HOPPER_VOLUME_LITERS)
     * @return true if batch completed successfully
     */
    bool _dispenseBatch();

    /**
     * @brief Calculate target weight for current batch
     * @return Batch target in grams (limited by hopper capacity and remaining)
     */
    float _calculateBatchTarget();

    /**
     * @brief Run auger to dispense specified weight from a tank
     * @param tankUid Tank to dispense from
     * @param targetGrams Weight to dispense
     * @param dispensedOut Output: actual weight dispensed
     * @return true if dispense completed successfully
     */
    bool _runAugerForIngredient(uint64_t tankUid, float targetGrams, float& dispensedOut);

    // --- Error Handling & Utilities ---

    /**
     * @brief Check if emergency stop has been commanded
     * @return true if emergency stop is active
     */
    bool _checkEmergencyStop();

    /**
     * @brief Handle dispensing error: log, set event, stop feeding
     * @param error The error that occurred
     */
    void _handleError(DispensingError error);

    /**
     * @brief Get density for a tank (kg/L -> g/L)
     * @param tankUid Tank UID
     * @return Kibble density in g/L, or 0 if tank not found
     */
    float _getTankDensityGramsPerLiter(uint64_t tankUid);
};

#endif // RECIPEPROCESSOR_HPP

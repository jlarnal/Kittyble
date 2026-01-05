#ifndef H_REED_SOLOMON_H
#define H_REED_SOLOMON_H

#include <cstdint>
#include <cstring> // For memset, memcpy
#include <cstdio>  // For printf/fprintf

// -------------------------------------------------------------------------
// Logging Configuration
// -------------------------------------------------------------------------
#if __has_include("esp_log.h")
#include "esp_log.h"
#else
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E [%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

// Defined as static to avoid linker errors if included in multiple .cpp files
static const char* RS_TAG = "ReedSolomon";

// -------------------------------------------------------------------------
// Galois Field (2^8) Arithmetic
// Primitive Polynomial: x^8 + x^4 + x^3 + x^2 + 1 (0x11D)
// -------------------------------------------------------------------------
class GaloisField {
  public:
    static constexpr int FIELD_SIZE = 256;

    // Tables stored in program memory (RODATA)
    static const uint8_t EXP_TABLE[FIELD_SIZE * 2];
    static const uint8_t LOG_TABLE[FIELD_SIZE];

    GaloisField() {}

    static const GaloisField& getInstance() {
        static GaloisField instance;
        return instance;
    }

    inline uint8_t add(uint8_t a, uint8_t b) const { return a ^ b; }
    inline uint8_t sub(uint8_t a, uint8_t b) const { return a ^ b; }

    inline uint8_t mul(uint8_t a, uint8_t b) const {
        if (a == 0 || b == 0) return 0;
        return EXP_TABLE[LOG_TABLE[a] + LOG_TABLE[b]];
    }

    inline uint8_t div(uint8_t a, uint8_t b) const {
        if (a == 0) return 0;
        if (b == 0) return 0;
        return EXP_TABLE[LOG_TABLE[a] + 255 - LOG_TABLE[b]];
    }

    inline uint8_t pow(uint8_t a, int n) const {
        if (n == 0) return 1;
        if (a == 0) return 0;
        return EXP_TABLE[((int)LOG_TABLE[a] * n) % 255];
    }

    inline uint8_t inverse(uint8_t a) const {
        if (a == 0) return 0;
        return EXP_TABLE[255 - LOG_TABLE[a]];
    }
};

// -------------------------------------------------------------------------
// Reed-Solomon Template Class
// DataLen: Number of message bytes
// EccLen: Number of parity bytes (must be even for standard correction)
// -------------------------------------------------------------------------
template <size_t DataLen, size_t EccLen>
class ReedSolomon {
public:
    static constexpr size_t BlockLen = DataLen + EccLen;
    static_assert(BlockLen <= 255, "Total block size (Data + ECC) must be <= 255 for GF(2^8)");

private:
    const GaloisField& gf;
    // C-style array member for generator polynomial
    uint8_t generatorPoly[EccLen];

    // Build Generator Polynomial: product of (x - alpha^0)(x - alpha^1)...
    void initGeneratorPoly() {
        memset(generatorPoly, 0, EccLen); 
        
        // Temporary buffer to compute polynomial expansion
        uint8_t g[EccLen + 1];
        memset(g, 0, sizeof(g));
        g[0] = 1;

        // Compute polynomial with roots alpha^0 to alpha^(EccLen-1)
        for (size_t i = 0; i < EccLen; i++) {
            uint8_t root = gf.EXP_TABLE[i]; // alpha^i
            
            // Multiply g(x) by (x + root)
            // g[j] = g[j-1] + g[j]*root
            for (size_t j = i + 1; j > 0; j--) {
                g[j] = gf.add(g[j-1], gf.mul(g[j], root));
            }
            g[0] = gf.mul(g[0], root);
        }

        // Store coefficients. 
        // g[EccLen] is always 1 (monic), so we only store g[0]...g[EccLen-1]
        for(size_t i=0; i < EccLen; i++) {
            generatorPoly[i] = g[i];
        }
    }

public:
    ReedSolomon() : gf(GaloisField::getInstance()) {
        initGeneratorPoly();
    }

    // -------------------------------------------------------------------------
    // Encoding
    // Input: data (DataLen bytes), ecc (EccLen bytes)
    // -------------------------------------------------------------------------
    void encode(const uint8_t* data, uint8_t* ecc) {
        if (ecc == nullptr) return;
        memset(ecc, 0, EccLen); 
        
        if (data == nullptr) return;

        for (size_t i = 0; i < DataLen; i++) {
            uint8_t feedback = gf.add(data[i], ecc[EccLen - 1]);

            for (size_t j = EccLen - 1; j > 0; j--) {
                ecc[j] = ecc[j - 1];
            }
            ecc[0] = 0;

            if (feedback != 0) {
                for (size_t j = 0; j < EccLen; j++) {
                    ecc[j] = gf.add(ecc[j], gf.mul(generatorPoly[j], feedback));
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Decoding
    // Input: received_data (DataLen), received_ecc (EccLen)
    // Returns: Number of errors corrected (-1 if uncorrectable, 0 if no errors)
    // -------------------------------------------------------------------------
    int decode(uint8_t* data, uint8_t* ecc) {
        // 1. Thread safety guard (since we use static buffers)
        static bool inUse = false;
        if (inUse) {
            ESP_LOGE(RS_TAG, "Concurrent call of ReedSolomon::decode");
            return -1;
        }
        inUse = true;

        // 2. Static buffers (No stack allocation)
        static uint8_t syndromes[EccLen];
        static uint8_t lambda[EccLen + 1];
        static uint8_t temp_lambda[EccLen + 1];
        static uint8_t b[EccLen + 1];
        static int errorLocations[EccLen];
        static uint8_t omega[EccLen];

        // Ensure clean state for syndromes check
        // (lambda, b, etc are reset below if errors found)
        memset(syndromes, 0, sizeof(syndromes));

        bool hasError = false;

        for (size_t i = 0; i < EccLen; i++) {
            uint8_t val = 0;
            uint8_t alpha_i = gf.EXP_TABLE[i]; 
            
            for (size_t j = 0; j < DataLen; j++) {
                val = gf.add(gf.mul(val, alpha_i), data[j]);
            }
            for (int j = EccLen - 1; j >= 0; j--) {
                 val = gf.add(gf.mul(val, alpha_i), ecc[j]);
            }

            syndromes[i] = val;
            if (val != 0) hasError = true;
        }

        if (!hasError) {
            inUse = false; // Release lock on success
            return 0;
        }

        // Initialize B-M variables
        memset(lambda, 0, sizeof(lambda));
        lambda[0] = 1;

        memset(b, 0, sizeof(b));
        b[0] = 1;
        
        int r_len = 0; 
        int k = 1;     
        
        for (int n = 0; n < (int)EccLen; n++) {
            uint8_t d = syndromes[n];
            for (int i = 1; i <= r_len; i++) {
                d = gf.add(d, gf.mul(lambda[i], syndromes[n - i]));
            }
            
            if (d == 0) {
                k++;
            } else {
                memcpy(temp_lambda, lambda, sizeof(lambda)); 
                
                for (int i = 0; i <= (int)EccLen - k; i++) {
                     if (i + k < (int)EccLen + 1) {
                         uint8_t term = gf.mul(d, b[i]);
                         lambda[i + k] = gf.add(lambda[i + k], term);
                     }
                }
                
                if (2 * r_len <= n) {
                    r_len = n + 1 - r_len;
                    uint8_t inv_d = gf.inverse(d);
                    for(size_t i=0; i < sizeof(b); i++) {
                        b[i] = gf.mul(temp_lambda[i], inv_d);
                    }
                    k = 1;
                } else {
                    k++;
                }
            }
        }
        
        // 3. Chien Search
        int errorCount = 0;
        for (int i = 0; i < (int)BlockLen; i++) {
            int j = i; 
            uint8_t invX = gf.EXP_TABLE[(255 - j) % 255]; 
            
            uint8_t val = 0;
            for (int m = r_len; m >= 0; m--) {
                val = gf.add(gf.mul(val, invX), lambda[m]);
            }
            
            if (val == 0) {
                if (errorCount >= (int)EccLen) {
                    inUse = false;
                    return -1; // Too many errors
                }
                errorLocations[errorCount++] = j; 
            }
        }
        
        if (errorCount != r_len) {
            inUse = false;
            return -1;
        }

        // 4. Forney Algorithm
        memset(omega, 0, sizeof(omega));
        for (int i = 0; i < (int)EccLen; i++) {
            for (int j = 0; j <= r_len; j++) {
                 if (i - j >= 0) {
                     omega[i] = gf.add(omega[i], gf.mul(syndromes[i - j], lambda[j]));
                 }
            }
        }
        
        for (int e = 0; e < errorCount; e++) {
            int loc = errorLocations[e]; 
            uint8_t invX = gf.EXP_TABLE[(255 - loc) % 255]; 
            
            uint8_t num = 0;
            for (int i = (int)EccLen - 1; i >= 0; i--) {
                num = gf.add(gf.mul(num, invX), omega[i]);
            }
            
            uint8_t den = 0;
            for (int i = 1; i <= r_len; i += 2) {
                uint8_t term = gf.mul(lambda[i], gf.pow(invX, i - 1));
                den = gf.add(den, term);
            }
            
            if (den == 0) {
                inUse = false;
                return -1;
            }

            // CRITICAL FIX: 
            // For generator roots starting at alpha^0 (b=0), the error magnitude formula
            // must be multiplied by X_k (the error location), not just evaluated at invX.
            // Previous incorrect code: uint8_t magnitude = gf.div(gf.mul(invX, num), den);
            
            uint8_t X = gf.inverse(invX); // This retrieves the actual error location X_k
            uint8_t magnitude = gf.div(gf.mul(X, num), den);
            
            if (loc < (int)EccLen) {
                ecc[loc] = gf.add(ecc[loc], magnitude);
            } else {
                int dataIdx = BlockLen - 1 - loc;
                if (dataIdx >= 0 && dataIdx < (int)DataLen) {
                    data[dataIdx] = gf.add(data[dataIdx], magnitude);
                }
            }
        }

        inUse = false;
        return errorCount;
    }
};

#endif // H_REED_SOLOMON_H

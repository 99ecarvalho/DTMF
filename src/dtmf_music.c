/*
 * dtmf_music.c
 *
 * MUSIC algorithm implementation for DTMF detection
 */

#include "dtmf_algorithms.h"
#include "dtmf_utils.h"

// =============================================================================
// MATRIX OPERATIONS
// =============================================================================

dtmf_result_t matrix_create(matrix_t *matrix, int rows, int cols) {
    if (!matrix || rows <= 0 || cols <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    matrix->data = malloc(rows * sizeof(double*));
    if (!matrix->data) {
        return DTMF_ERROR_MEMORY;
    }
    
    for (int i = 0; i < rows; i++) {
        matrix->data[i] = calloc(cols, sizeof(double));
        if (!matrix->data[i]) {
            // Cleanup previously allocated rows
            for (int j = 0; j < i; j++) {
                free(matrix->data[j]);
            }
            free(matrix->data);
            return DTMF_ERROR_MEMORY;
        }
    }
    
    matrix->rows = rows;
    matrix->cols = cols;
    return DTMF_SUCCESS;
}

void matrix_destroy(matrix_t *matrix) {
    if (matrix && matrix->data) {
        for (int i = 0; i < matrix->rows; i++) {
            free(matrix->data[i]);
        }
        free(matrix->data);
        matrix->data = NULL;
        matrix->rows = 0;
        matrix->cols = 0;
    }
}

dtmf_result_t matrix_multiply(const matrix_t *a, const matrix_t *b, matrix_t *result) {
    if (!a || !b || !result || a->cols != b->rows) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    dtmf_result_t status = matrix_create(result, a->rows, b->cols);
    if (status != DTMF_SUCCESS) {
        return status;
    }
    
    for (int i = 0; i < a->rows; i++) {
        for (int j = 0; j < b->cols; j++) {
            result->data[i][j] = 0.0;
            for (int k = 0; k < a->cols; k++) {
                result->data[i][j] += a->data[i][k] * b->data[k][j];
            }
        }
    }
    
    return DTMF_SUCCESS;
}

dtmf_result_t matrix_transpose(const matrix_t *input, matrix_t *result) {
    if (!input || !result) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    dtmf_result_t status = matrix_create(result, input->cols, input->rows);
    if (status != DTMF_SUCCESS) {
        return status;
    }
    
    for (int i = 0; i < input->rows; i++) {
        for (int j = 0; j < input->cols; j++) {
            result->data[j][i] = input->data[i][j];
        }
    }
    
    return DTMF_SUCCESS;
}

// Simple eigenvalue decomposition for symmetric matrices (Jacobi method)
dtmf_result_t symmetric_eigendecomposition(matrix_t *matrix, 
                                          double *eigenvalues,
                                          matrix_t *eigenvectors) {
    if (!matrix || !eigenvalues || !eigenvectors || matrix->rows != matrix->cols) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    int n = matrix->rows;
    const int max_iterations = 100;
    const double tolerance = 1e-10;
    
    // Initialize eigenvectors as identity matrix
    dtmf_result_t status = matrix_create(eigenvectors, n, n);
    if (status != DTMF_SUCCESS) {
        return status;
    }
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            eigenvectors->data[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }
    
    // Copy matrix data for in-place operations
    matrix_t a;
    status = matrix_create(&a, n, n);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(eigenvectors);
        return status;
    }
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            a.data[i][j] = matrix->data[i][j];
        }
    }
    
    // Jacobi iteration
    for (int iter = 0; iter < max_iterations; iter++) {
        // Find largest off-diagonal element
        double max_val = 0.0;
        int p = 0, q = 1;
        
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (fabs(a.data[i][j]) > max_val) {
                    max_val = fabs(a.data[i][j]);
                    p = i;
                    q = j;
                }
            }
        }
        
        if (max_val < tolerance) {
            break; // Converged
        }
        
        // Calculate rotation angle
        double theta;
        if (fabs(a.data[p][p] - a.data[q][q]) < tolerance) {
            theta = M_PI / 4.0;
        } else {
            theta = 0.5 * atan(2.0 * a.data[p][q] / (a.data[p][p] - a.data[q][q]));
        }
        
        double c = cos(theta);
        double s = sin(theta);
        
        // Apply Givens rotation
        for (int i = 0; i < n; i++) {
            if (i != p && i != q) {
                double aip = a.data[i][p];
                double aiq = a.data[i][q];
                a.data[i][p] = c * aip - s * aiq;
                a.data[p][i] = a.data[i][p];
                a.data[i][q] = s * aip + c * aiq;
                a.data[q][i] = a.data[i][q];
            }
        }
        
        double app = a.data[p][p];
        double aqq = a.data[q][q];
        double apq = a.data[p][q];
        
        a.data[p][p] = c * c * app - 2.0 * c * s * apq + s * s * aqq;
        a.data[q][q] = s * s * app + 2.0 * c * s * apq + c * c * aqq;
        a.data[p][q] = 0.0;
        a.data[q][p] = 0.0;
        
        // Update eigenvectors
        for (int i = 0; i < n; i++) {
            double vip = eigenvectors->data[i][p];
            double viq = eigenvectors->data[i][q];
            eigenvectors->data[i][p] = c * vip - s * viq;
            eigenvectors->data[i][q] = s * vip + c * viq;
        }
    }
    
    // Extract eigenvalues from diagonal
    for (int i = 0; i < n; i++) {
        eigenvalues[i] = a.data[i][i];
    }
    
    matrix_destroy(&a);
    return DTMF_SUCCESS;
}

dtmf_result_t create_hankel_matrix(const int16_t *samples, 
                                  size_t sample_count,
                                  int L,
                                  matrix_t *hankel) {
    if (!samples || sample_count == 0 || L <= 0 || !hankel) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    int M = (int)sample_count - L + 1;
    if (M <= 0) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    dtmf_result_t status = matrix_create(hankel, L, M);
    if (status != DTMF_SUCCESS) {
        return status;
    }
    
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < M; j++) {
            hankel->data[i][j] = (double)samples[i + j];
        }
    }
    
    return DTMF_SUCCESS;
}

double music_pseudospectrum(const matrix_t *noise_subspace,
                           double frequency,
                           double sample_rate,
                           int L) {
    if (!noise_subspace || frequency <= 0 || sample_rate <= 0 || L <= 0) {
        return 0.0;
    }
    
    // Create steering vector for the frequency
    complex_t *steering = malloc(L * sizeof(complex_t));
    if (!steering) {
        return 0.0;
    }
    
    double omega = 2.0 * M_PI * frequency / sample_rate;
    for (int i = 0; i < L; i++) {
        steering[i].re = cos(omega * i);
        steering[i].im = sin(omega * i);
    }
    
    // Calculate a^H * En * En^H * a
    double numerator = 0.0;
    
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < L; j++) {
            // steering[i]* * noise_subspace * noise_subspace^H * steering[j]
            double sum_real = 0.0;
            
            for (int k = 0; k < noise_subspace->cols; k++) {
                // noise_subspace[i][k] * noise_subspace[j][k]*
                sum_real += noise_subspace->data[i][k] * noise_subspace->data[j][k];
            }
            
            // steering[i]* * sum * steering[j]
            numerator += (steering[i].re * sum_real + steering[i].im * 0.0) * steering[j].re +
                        (steering[i].im * sum_real - steering[i].re * 0.0) * steering[j].im;
        }
    }
    
    free(steering);
    
    // MUSIC pseudospectrum is 1 / (denominator)
    return (numerator > 0.0) ? 1.0 / numerator : 0.0;
}

// =============================================================================
// MUSIC ANALYSIS
// =============================================================================

dtmf_result_t analyze_music_strategy(const audio_context_t *ctx,
                                    const dtmf_config_t *config,
                                    const window_strategy_t *window_strategy,
                                    dtmf_analysis_result_t *result) {
    if (!ctx || !config || !result) {
        return DTMF_ERROR_INVALID_PARAM;
    }
    
    clock_t start_time = clock();
    
    // Initialize result
    result->low_tone.index = 0;
    result->low_tone.frequency = 0.0;
    result->low_tone.magnitude = 0.0;
    result->high_tone.index = 0;
    result->high_tone.frequency = 0.0;
    result->high_tone.magnitude = 0.0;
    result->low_confidence = 0.0;
    result->high_confidence = 0.0;
    result->detected_key = '\0';
    result->window_name = "MUSIC";
    
    // Get the windowed samples
    const int16_t *samples = ctx->samples;
    size_t sample_count = ctx->sample_count;
    
    // Apply windowing
    int16_t *windowed_samples = malloc(sample_count * sizeof(int16_t));
    if (!windowed_samples) {
        return DTMF_ERROR_MEMORY;
    }
    
    for (size_t i = 0; i < sample_count; i++) {
        double window_value = 1.0; // Default (rectangular)
        if (window_strategy && strcmp(window_strategy->name, "Hanning") == 0) {
            window_value = 0.5 * (1.0 - cos(2.0 * M_PI * i / (sample_count - 1)));
        } else if (window_strategy && strcmp(window_strategy->name, "Hamming") == 0) {
            window_value = 0.54 - 0.46 * cos(2.0 * M_PI * i / (sample_count - 1));
        } else if (window_strategy && strcmp(window_strategy->name, "Blackman") == 0) {
            window_value = 0.42 - 0.5 * cos(2.0 * M_PI * i / (sample_count - 1)) +
                          0.08 * cos(4.0 * M_PI * i / (sample_count - 1));
        }
        windowed_samples[i] = (int16_t)(samples[i] * window_value);
    }
    
    // MUSIC parameters
    int L = sample_count / 3;  // Subspace dimension (typically N/3)
    if (L < 10) {
        L = 10;  // Minimum subspace size
    }
    
    int noise_dim = L - 2;  // Assume 2 signal components (low + high tone)
    if (noise_dim <= 0) {
        noise_dim = L / 2;
    }
    
    matrix_t hankel, correlation, eigenvectors;
    double *eigenvalues = NULL;
    
    // Create Hankel matrix
    dtmf_result_t status = create_hankel_matrix(windowed_samples, sample_count, L, &hankel);
    if (status != DTMF_SUCCESS) {
        free(windowed_samples);
        return status;
    }
    
    // Compute autocorrelation matrix R = H * H^T
    matrix_t hankel_transpose;
    status = matrix_transpose(&hankel, &hankel_transpose);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(&hankel);
        free(windowed_samples);
        return status;
    }
    
    status = matrix_multiply(&hankel, &hankel_transpose, &correlation);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(&hankel);
        matrix_destroy(&hankel_transpose);
        free(windowed_samples);
        return status;
    }
    
    matrix_destroy(&hankel);
    matrix_destroy(&hankel_transpose);
    
    // Eigendecomposition
    eigenvalues = malloc(L * sizeof(double));
    if (!eigenvalues) {
        matrix_destroy(&correlation);
        free(windowed_samples);
        return DTMF_ERROR_MEMORY;
    }
    
    status = symmetric_eigendecomposition(&correlation, eigenvalues, &eigenvectors);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(&correlation);
        free(eigenvalues);
        free(windowed_samples);
        return status;
    }
    
    matrix_destroy(&correlation);
    
    // Sort eigenvalues and eigenvectors in descending order
    for (int i = 0; i < L - 1; i++) {
        for (int j = i + 1; j < L; j++) {
            if (eigenvalues[i] < eigenvalues[j]) {
                // Swap eigenvalues
                double temp = eigenvalues[i];
                eigenvalues[i] = eigenvalues[j];
                eigenvalues[j] = temp;
                
                // Swap corresponding eigenvectors
                for (int k = 0; k < L; k++) {
                    double temp_vec = eigenvectors.data[k][i];
                    eigenvectors.data[k][i] = eigenvectors.data[k][j];
                    eigenvectors.data[k][j] = temp_vec;
                }
            }
        }
    }
    
    // Extract noise subspace (last 'noise_dim' eigenvectors)
    matrix_t noise_subspace;
    status = matrix_create(&noise_subspace, L, noise_dim);
    if (status != DTMF_SUCCESS) {
        matrix_destroy(&eigenvectors);
        free(eigenvalues);
        free(windowed_samples);
        return status;
    }
    
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < noise_dim; j++) {
            noise_subspace.data[i][j] = eigenvectors.data[i][L - noise_dim + j];
        }
    }
    
    matrix_destroy(&eigenvectors);
    free(eigenvalues);
    
    // Calculate MUSIC pseudospectrum for each DTMF frequency (separate low/high)
    double low_pseudospectra[config->num_low_freqs];
    double high_pseudospectra[config->num_high_freqs];
    
    // Initialize arrays to zero
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_pseudospectra[i] = 0.0;
    }
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_pseudospectra[i] = 0.0;
    }
    
    for (size_t i = 0; i < config->num_low_freqs; i++) {
        low_pseudospectra[i] = music_pseudospectrum(&noise_subspace, 
                                                   config->low_frequencies[i],
                                                   (double)ctx->sample_rate, 
                                                   L);
    }
    
    for (size_t i = 0; i < config->num_high_freqs; i++) {
        high_pseudospectra[i] = music_pseudospectrum(&noise_subspace, 
                                                    config->high_frequencies[i],
                                                    (double)ctx->sample_rate, 
                                                    L);
    }
    
    matrix_destroy(&noise_subspace);
    
    // Find best tones using the existing helper functions
    result->low_tone = find_maximum_tone(low_pseudospectra, config->low_frequencies, config->num_low_freqs);
    result->high_tone = find_maximum_tone(high_pseudospectra, config->high_frequencies, config->num_high_freqs);
    
    // Calculate confidence ratios
    result->low_confidence = calculate_confidence_ratio(low_pseudospectra, config->num_low_freqs, result->low_tone.index);
    result->high_confidence = calculate_confidence_ratio(high_pseudospectra, config->num_high_freqs, result->high_tone.index);
    
    // Map to DTMF key
    result->detected_key = map_dtmf_key(config, result->low_tone.index, result->high_tone.index);
    
    free(windowed_samples);
    
    clock_t end_time = clock();
    result->timing.wall_time_s = ((double)(end_time - start_time) / CLOCKS_PER_SEC);
    result->timing.cpu_time_s = result->timing.wall_time_s; // Simplified
    
    return DTMF_SUCCESS;
}

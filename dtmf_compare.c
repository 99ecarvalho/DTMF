/*
 * dtmf_compare.c
 *
 * Usage:
 *   ./dtmf_compare <raw_pcm16_file> [sample_rate] [window_ms] [zero_pad_N]
 *
 * - raw_pcm16_file: 16-bit signed little-endian PCM mono.
 * - sample_rate: default 8000
 * - window_ms: analysis window in milliseconds (default 40)
 * - zero_pad_N: FFT size (power of two). If 0 or omitted, next power of two >= samples_in_window is used.
 *
 * The program:
 *  - reads the first window_ms of samples from the file,
 *  - applies a Hamming window,
 *  - runs FFT (with zero padding) and finds DTMF tones,
 *  - runs Goertzel across the DTMF frequencies and finds tones,
 *  - prints magnitudes and chosen digit for each method,
 *  - prints wall time and process CPU time for each method for comparison.
 *
 * Build:
 *   make
 *
 * Note: this is a single-file implementation with a simple FFT (radix-2, iterative).
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#define _GNU_SOURCE
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { double re, im; } cpx;

static inline cpx cpx_new(double r, double i){ cpx z; z.re = r; z.im = i; return z; }
static inline cpx cpx_add(cpx a, cpx b){ return cpx_new(a.re + b.re, a.im + b.im); }
static inline cpx cpx_sub(cpx a, cpx b){ return cpx_new(a.re - b.re, a.im - b.im); }
static inline cpx cpx_mul(cpx a, cpx b){ return cpx_new(a.re*b.re - a.im*b.im, a.re*b.im + a.im*b.re); }
static inline double cpx_abs(cpx a){ return sqrt(a.re*a.re + a.im*a.im); }

static int is_power_of_two(int x){ return x && ((x & (x - 1)) == 0); }
static int next_power_of_two(int x){
    int p = 1;
    while(p < x) p <<= 1;
    return p;
}

/* iterative in-place radix-2 FFT (dir = +1 for forward) */
void fft(cpx *buf, int n, int dir) {
    // bit reversal
    int i, j, k;
    for(i = 1, j = 0; i < n; i++){
        int bit = n >> 1;
        for(; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if(i < j) { cpx t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
    }

    for(k = 1; k < n; k <<= 1){
        int step = k << 1;
        double theta = dir * (-M_PI / k); // negative for forward so that e^{-j*2pi*k/N}
        for(i = 0; i < n; i += step){
            for(j = 0; j < k; j++){
                double angle = theta * j;
                double w_re = cos(angle);
                double w_im = sin(angle);
                cpx w = cpx_new(w_re, w_im);
                cpx u = buf[i + j];
                cpx v = cpx_mul(w, buf[i + j + k]);
                buf[i + j] = cpx_add(u, v);
                buf[i + j + k] = cpx_sub(u, v);
            }
        }
    }

    if(dir == -1){ // inverse scale
        for(i = 0; i < n; i++){ buf[i].re /= n; buf[i].im /= n; }
    }
}

/* Hamming window */
void apply_hamming(double *x, int n) {
    for(int i = 0; i < n; ++i){
        double w = 0.54 - 0.46 * cos(2.0 * M_PI * i / (n - 1));
        x[i] *= w;
    }
}

/* Goertzel algorithm to compute magnitude at freq f (Hz) given sample rate fs */
double goertzel_magnitude(const int16_t *samples, int N, double fs, double f) {
    double k = 0.5 + ((N * f) / fs);
    double omega = 2.0 * M_PI * k / N;
    double coeff = 2.0 * cos(omega);
    double q0 = 0.0, q1 = 0.0, q2 = 0.0;
    for(int i = 0; i < N; ++i){
        q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }
    double re = q1 - q2 * cos(omega);
    double im = q2 * sin(omega);
    return sqrt(re*re + im*im);
}

/* DTMF freq lists and map */
static const double dtmf_low[]  = {697.0, 770.0, 852.0, 941.0};
static const double dtmf_high[] = {1209.0, 1336.0, 1477.0, 1633.0};
static const char dtmf_keys[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

char map_to_key(int low_idx, int high_idx){
    if(low_idx < 0 || low_idx > 3 || high_idx < 0 || high_idx > 3) return '?';
    return dtmf_keys[low_idx][high_idx];
}

/* timing helpers */
void timespec_diff(struct timespec *start, struct timespec *end, struct timespec *out){
    out->tv_sec = end->tv_sec - start->tv_sec;
    out->tv_nsec = end->tv_nsec - start->tv_nsec;
    if(out->tv_nsec < 0){ out->tv_nsec += 1000000000L; out->tv_sec -= 1; }
}

double ts_to_double(const struct timespec *t){ return t->tv_sec + t->tv_nsec / 1e9; }

int main(int argc, char **argv){
    if(argc < 2){
        fprintf(stderr, "Usage: %s <raw_pcm16_file> [sample_rate=8000] [window_ms=40] [zero_pad_N=0]\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    int fs = (argc >= 3) ? atoi(argv[2]) : 8000;
    int window_ms = (argc >= 4) ? atoi(argv[3]) : 40;
    int zero_pad_N = (argc >= 5) ? atoi(argv[4]) : 0;

    int window_samples = (fs * window_ms) / 1000;
    if(window_samples < 8) window_samples = 8;

    FILE *f = fopen(filename, "rb");
    if(!f){ perror("fopen"); return 1; }

    int16_t *samples = malloc(sizeof(int16_t) * window_samples);
    if(!samples){ perror("malloc"); fclose(f); return 1; }

    size_t read = fread(samples, sizeof(int16_t), window_samples, f);
    fclose(f);

    if((int)read < window_samples){
        fprintf(stderr, "Warning: file shorter than requested window (%d samples). Using %zu samples.\n", window_samples, read);
        window_samples = (int)read;
    }

    /* Convert to double and apply Hamming */
    double *x = malloc(sizeof(double) * window_samples);
    for(int i = 0; i < window_samples; ++i) x[i] = (double)samples[i];
    apply_hamming(x, window_samples);

    /* FFT preparation */
    int Nfft = zero_pad_N;
    if(Nfft <= 0) {
        Nfft = next_power_of_two(window_samples);
    } else {
        if(!is_power_of_two(Nfft)){
            fprintf(stderr, "zero_pad_N must be power of two. Using next power of two.\n");
            Nfft = next_power_of_two(Nfft);
        }
        if(Nfft < window_samples) {
            fprintf(stderr, "zero_pad_N < window length; using next pow2 >= window_samples.\n");
            Nfft = next_power_of_two(window_samples);
        }
    }

    cpx *buf = malloc(sizeof(cpx) * Nfft);
    if(!buf){ perror("malloc buf"); free(samples); free(x); return 1; }

    for(int i = 0; i < window_samples; ++i){ buf[i].re = x[i]; buf[i].im = 0.0; }
    for(int i = window_samples; i < Nfft; ++i){ buf[i].re = 0.0; buf[i].im = 0.0; }

    /* FFT run & timing */
    struct timespec ft_start_wall, ft_end_wall, ft_start_cpu, ft_end_cpu;
    clock_gettime(CLOCK_MONOTONIC, &ft_start_wall);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ft_start_cpu);

    fft(buf, Nfft, 1); // forward

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ft_end_cpu);
    clock_gettime(CLOCK_MONOTONIC, &ft_end_wall);

    struct timespec ft_wall_diff, ft_cpu_diff;
    timespec_diff(&ft_start_wall, &ft_end_wall, &ft_wall_diff);
    timespec_diff(&ft_start_cpu, &ft_end_cpu, &ft_cpu_diff);

    double fft_wall = ts_to_double(&ft_wall_diff);
    double fft_cpu  = ts_to_double(&ft_cpu_diff);

    /* Find magnitudes at positive freqs only */
    int half = Nfft / 2;
    double freq_res = (double)fs / Nfft;

    /* compute magnitudes for DTMF frequencies by finding nearest bin magnitudes */
    double low_mags[4] = {0}, high_mags[4] = {0};
    for(int li = 0; li < 4; ++li){
        double target = dtmf_low[li];
        int bin = (int)round(target / freq_res);
        if(bin < 0) bin = 0;
        if(bin > half-1) bin = half-1;
        low_mags[li] = cpx_abs(buf[bin]);
    }
    for(int hi = 0; hi < 4; ++hi){
        double target = dtmf_high[hi];
        int bin = (int)round(target / freq_res);
        if(bin < 0) bin = 0;
        if(bin > half-1) bin = half-1;
        high_mags[hi] = cpx_abs(buf[bin]);
    }

    int best_low_idx = 0; double best_low_val = low_mags[0];
    for(int i = 1; i < 4; ++i) if(low_mags[i] > best_low_val){ best_low_val = low_mags[i]; best_low_idx = i; }
    int best_high_idx = 0; double best_high_val = high_mags[0];
    for(int i = 1; i < 4; ++i) if(high_mags[i] > best_high_val){ best_high_val = high_mags[i]; best_high_idx = i; }

    char fft_key = map_to_key(best_low_idx, best_high_idx);

    /* Goertzel run & timing */
    struct timespec gt_start_wall, gt_end_wall, gt_start_cpu, gt_end_cpu;
    clock_gettime(CLOCK_MONOTONIC, &gt_start_wall);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &gt_start_cpu);

    double g_low_mags[4] = {0}, g_high_mags[4] = {0};
    for(int li = 0; li < 4; ++li) g_low_mags[li] = goertzel_magnitude(samples, window_samples, fs, dtmf_low[li]);
    for(int hi = 0; hi < 4; ++hi) g_high_mags[hi] = goertzel_magnitude(samples, window_samples, fs, dtmf_high[hi]);

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &gt_end_cpu);
    clock_gettime(CLOCK_MONOTONIC, &gt_end_wall);

    struct timespec gt_wall_diff, gt_cpu_diff;
    timespec_diff(&gt_start_wall, &gt_end_wall, &gt_wall_diff);
    timespec_diff(&gt_start_cpu, &gt_end_cpu, &gt_cpu_diff);
    double goertzel_wall = ts_to_double(&gt_wall_diff);
    double goertzel_cpu  = ts_to_double(&gt_cpu_diff);

    int g_best_low_idx = 0; double g_best_low_val = g_low_mags[0];
    for(int i = 1; i < 4; ++i) if(g_low_mags[i] > g_best_low_val){ g_best_low_val = g_low_mags[i]; g_best_low_idx = i; }
    int g_best_high_idx = 0; double g_best_high_val = g_high_mags[0];
    for(int i = 1; i < 4; ++i) if(g_high_mags[i] > g_best_high_val){ g_best_high_val = g_high_mags[i]; g_best_high_idx = i; }
    char g_key = map_to_key(g_best_low_idx, g_best_high_idx);

    /* Print results */
    printf("Parameters:\n");
    printf("  sample_rate = %d Hz\n  window_ms = %d ms -> window_samples = %d\n  FFT size (Nfft) = %d, freq_res = %.3f Hz\n\n",
        fs, window_ms, window_samples, Nfft, freq_res);

    printf("FFT with zero-padding results:\n");
    for(int i = 0; i < 4; ++i) printf("  low %5.1f Hz: mag = %.6f\n", dtmf_low[i], low_mags[i]);
    for(int i = 0; i < 4; ++i) printf("  high%5.1f Hz: mag = %.6f\n", dtmf_high[i], high_mags[i]);
    printf("  best low idx = %d (%.1f Hz), best high idx = %d (%.1f Hz)\n",
        best_low_idx, dtmf_low[best_low_idx], best_high_idx, dtmf_high[best_high_idx]);
    printf("  detected key (FFT) = %c\n", fft_key);
    printf("  FFT time: wall = %.6f s, cpu = %.6f s\n\n", fft_wall, fft_cpu);

    printf("Goertzel results:\n");
    for(int i = 0; i < 4; ++i) printf("  low %5.1f Hz: mag = %.6f\n", dtmf_low[i], g_low_mags[i]);
    for(int i = 0; i < 4; ++i) printf("  high%5.1f Hz: mag = %.6f\n", dtmf_high[i], g_high_mags[i]);
    printf("  best low idx = %d (%.1f Hz), best high idx = %d (%.1f Hz)\n",
        g_best_low_idx, dtmf_low[g_best_low_idx], g_best_high_idx, dtmf_high[g_best_high_idx]);
    printf("  detected key (Goertzel) = %c\n", g_key);
    printf("  Goertzel time: wall = %.6f s, cpu = %.6f s\n\n", goertzel_wall, goertzel_cpu);

    /* Simple check: compare keys and print a basic confidence estimate (ratio between best and second best) */
    double low_second = -1.0, high_second = -1.0;
    for(int i = 0; i < 4; ++i) if(i != best_low_idx && low_mags[i] > low_second) low_second = low_mags[i];
    for(int i = 0; i < 4; ++i) if(i != best_high_idx && high_mags[i] > high_second) high_second = high_mags[i];
    double low_conf = (low_second > 0) ? best_low_val / low_second : 1e9;
    double high_conf = (high_second > 0) ? best_high_val / high_second : 1e9;
    printf("FFT confidence ratios: low = %.3f, high = %.3f\n", low_conf, high_conf);

    double glow_second = -1.0, ghigh_second = -1.0;
    for(int i = 0; i < 4; ++i) if(i != g_best_low_idx && g_low_mags[i] > glow_second) glow_second = g_low_mags[i];
    for(int i = 0; i < 4; ++i) if(i != g_best_high_idx && g_high_mags[i] > ghigh_second) ghigh_second = g_high_mags[i];
    double gl_conf = (glow_second > 0) ? g_best_low_val / glow_second : 1e9;
    double gh_conf = (ghigh_second > 0) ? g_best_high_val / ghigh_second : 1e9;
    printf("Goertzel confidence ratios: low = %.3f, high = %.3f\n", gl_conf, gh_conf);

    /* cleanup */
    free(samples);
    free(x);
    free(buf);

    return 0;
}

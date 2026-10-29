/*
 * ordenacao.c - Competição de Ordenação: O Desafio dos Milissegundos
 *
 * Estratégia (não comparativa):
 *   1. Counting Sort com deslocamento por mínimo (faixa = max - min + 1),
 *      O(n + k). Aceita negativos e positivos.
 *   2. Se a faixa for grande demais para a memória (ou grande demais em
 *      relação a n), usa Radix Sort LSD de 32 bits com sinal (3 passadas de
 *      11/11/10 bits), O(n).
 *
 * Compilar:
 *   gcc -O3 -march=native -flto -fomit-frame-pointer -Wall -Wextra \
 *       -o ordenacao ordenacao.c
 * Executar:
 *   ./ordenacao ListaASerOrdenada.txt
 *
 * Cronômetro (clock): envolve SOMENTE ordenar_vetor(), que inclui a busca
 * de min/max, a alocação auxiliar e a ordenação em si. Leitura do arquivo,
 * checksum e verificação ficam fora da medição.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Limites de memória do Counting Sort: no máximo 2^24 contadores (64 MB) e
 * no máximo 4n + 65536 contadores, para não alocar uma faixa enorme em
 * entradas pequenas. Acima disso, Radix Sort. */
#define COUNTING_MAX_FAIXA   ((uint64_t)1 << 24)
#define COUNTING_FATOR_N     4u
#define COUNTING_FOLGA       65536u

/* ---------------------------------------------------------------------- */
/* Utilitários                                                            */
/* ---------------------------------------------------------------------- */

static void morrer(const char *msg) {
    fprintf(stderr, "Erro: %s\n", msg);
    exit(EXIT_FAILURE);
}

/* Lê o arquivo inteiro em memória e converte em int32_t (um por linha).
 * Aceita sinal '-'/'+', espaços, \r\n. Fora da medição. */
static int32_t *ler_arquivo(const char *nome, size_t *n_out) {
    FILE *f = fopen(nome, "rb");
    if (!f) { perror("fopen"); exit(EXIT_FAILURE); }
    if (fseek(f, 0, SEEK_END) != 0) morrer("fseek");
    long tam = ftell(f);
    if (tam < 0) morrer("ftell");
    rewind(f);

    char *buf = malloc((size_t)tam + 1);
    if (!buf) morrer("sem memoria para o buffer de leitura");
    if (fread(buf, 1, (size_t)tam, f) != (size_t)tam) morrer("fread");
    fclose(f);
    buf[tam] = '\0';

    /* Limite superior de elementos: número de '\n' + 1. */
    size_t cap = 1;
    for (long i = 0; i < tam; i++) cap += (buf[i] == '\n');
    int32_t *v = malloc(cap * sizeof(int32_t));
    if (!v) morrer("sem memoria para o vetor");

    size_t n = 0;
    const char *p = buf, *fim = buf + tam;
    while (p < fim) {
        while (p < fim && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p >= fim) break;
        int neg = 0;
        if (*p == '-') { neg = 1; p++; } else if (*p == '+') { p++; }
        if (*p < '0' || *p > '9') morrer("linha invalida no arquivo de entrada");
        int64_t x = 0;
        while (p < fim && *p >= '0' && *p <= '9') { x = x * 10 + (*p - '0'); p++; }
        if (neg) x = -x;
        if (x < INT32_MIN || x > INT32_MAX) morrer("valor fora do intervalo de int32");
        if (n >= cap) morrer("contagem de linhas inconsistente");
        v[n++] = (int32_t)x;
    }
    free(buf);
    *n_out = n;
    return v;
}

/* ---------------------------------------------------------------------- */
/* Ordenação                                                              */
/* ---------------------------------------------------------------------- */

/* Mínimo e máximo em uma passada, desenrolado 4x com acumuladores
 * independentes (quebra a dependência entre iterações e deixa o compilador
 * vetorizar). Faz parte da ordenação e está DENTRO do cronômetro. */
static void min_max(const int32_t *v, size_t n, int32_t *mn, int32_t *mx) {
    int32_t mn0 = v[0], mn1 = v[0], mn2 = v[0], mn3 = v[0];
    int32_t mx0 = v[0], mx1 = v[0], mx2 = v[0], mx3 = v[0];
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        int32_t a = v[i], b = v[i + 1], c = v[i + 2], d = v[i + 3];
        mn0 = a < mn0 ? a : mn0;  mx0 = a > mx0 ? a : mx0;
        mn1 = b < mn1 ? b : mn1;  mx1 = b > mx1 ? b : mx1;
        mn2 = c < mn2 ? c : mn2;  mx2 = c > mx2 ? c : mx2;
        mn3 = d < mn3 ? d : mn3;  mx3 = d > mx3 ? d : mx3;
    }
    for (; i < n; i++) {
        mn0 = v[i] < mn0 ? v[i] : mn0;
        mx0 = v[i] > mx0 ? v[i] : mx0;
    }
    mn0 = mn1 < mn0 ? mn1 : mn0;  mn2 = mn3 < mn2 ? mn3 : mn2;  mn0 = mn2 < mn0 ? mn2 : mn0;
    mx0 = mx1 > mx0 ? mx1 : mx0;  mx2 = mx3 > mx2 ? mx3 : mx2;  mx0 = mx2 > mx0 ? mx2 : mx0;
    *mn = mn0;
    *mx = mx0;
}

/* Counting Sort com deslocamento: cont[x - mn] conta ocorrências de x. */
static void counting_sort(int32_t *v, size_t n, int32_t mn, size_t faixa) {
    uint32_t *cont = calloc(faixa, sizeof(uint32_t));
    if (!cont) morrer("sem memoria para o vetor de contagem");

    for (size_t i = 0; i < n; i++) cont[(uint32_t)v[i] - (uint32_t)mn]++;

    size_t k = 0;
    for (size_t j = 0; j < faixa; j++) {
        uint32_t c = cont[j];
        int32_t x = (int32_t)((uint32_t)mn + (uint32_t)j);
        for (uint32_t r = 0; r < c; r++) v[k + r] = x;
        k += c;
    }
    free(cont);
}

/* Radix Sort LSD de 32 bits com sinal. Chave = valor ^ 0x80000000 (inverte o
 * bit de sinal, então a ordem como inteiro sem sinal = ordem como int com
 * sinal). Passadas de 11, 11 e 10 bits; passadas em que todos os elementos
 * caem no mesmo balde são puladas. */
static void radix_sort(int32_t *v, size_t n) {
    enum { P = 3 };
    static const int shift[P] = {0, 11, 22};
    static const uint32_t mask[P] = {2047u, 2047u, 1023u};
    static const size_t baldes[P] = {2048, 2048, 1024};

    uint32_t *aux = malloc(n * sizeof(uint32_t));
    size_t *hist[P];
    for (int p = 0; p < P; p++) {
        hist[p] = calloc(baldes[p] + 1, sizeof(size_t));
        if (!hist[p]) morrer("sem memoria para histogramas");
    }
    if (!aux) morrer("sem memoria para o vetor auxiliar");

    uint32_t *src = (uint32_t *)v, *dst = aux;

    /* Um único percurso monta os três histogramas. */
    for (size_t i = 0; i < n; i++) {
        uint32_t k = src[i] ^ 0x80000000u;
        for (int p = 0; p < P; p++) hist[p][((k >> shift[p]) & mask[p]) + 1]++;
    }

    for (int p = 0; p < P; p++) {
        size_t *h = hist[p];
        int trivial = 0;
        for (size_t b = 0; b < baldes[p]; b++) if (h[b + 1] == n) trivial = 1;
        if (trivial) continue;                       /* todos no mesmo balde */
        for (size_t b = 0; b < baldes[p]; b++) h[b + 1] += h[b];
        for (size_t i = 0; i < n; i++) {
            uint32_t k = src[i] ^ 0x80000000u;
            dst[h[(k >> shift[p]) & mask[p]]++] = src[i];
        }
        uint32_t *t = src; src = dst; dst = t;
    }
    if (src != (uint32_t *)v) memcpy(v, src, n * sizeof(int32_t));

    for (int p = 0; p < P; p++) free(hist[p]);
    free(aux);
}

/* Ponto de entrada da ordenação: tudo aqui está dentro do cronômetro. */
static void ordenar_vetor(int32_t *v, size_t n) {
    if (n < 2) return;
    int32_t mn, mx;
    min_max(v, n, &mn, &mx);
    uint64_t faixa = (uint64_t)((int64_t)mx - (int64_t)mn) + 1u;   /* até 2^32 */

    if (faixa <= COUNTING_MAX_FAIXA &&
        faixa <= (uint64_t)n * COUNTING_FATOR_N + COUNTING_FOLGA)
        counting_sort(v, n, mn, (size_t)faixa);
    else
        radix_sort(v, n);
}

/* ---------------------------------------------------------------------- */
/* Verificação (fora do cronômetro)                                       */
/* ---------------------------------------------------------------------- */

static uint64_t mix(uint64_t z) {            /* splitmix64 */
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* Assinatura de multiconjunto: soma dos hashes de cada elemento. Não depende
 * da ordem; se algum elemento sumir, mudar ou aparecer, a assinatura muda. */
static uint64_t assinatura(const int32_t *v, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += mix((uint64_t)(uint32_t)v[i]);
    return s;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <arquivo.txt>\n", argv[0]);
        return EXIT_FAILURE;
    }

    size_t n;
    int32_t *v = ler_arquivo(argv[1], &n);
    uint64_t ass_antes = assinatura(v, n);

    clock_t inicio = clock();
    ordenar_vetor(v, n);
    clock_t fim = clock();

    double ms = (double)(fim - inicio) / CLOCKS_PER_SEC * 1000.0;
    printf("Tempo de ordenacao: %.3f ms\n", ms);

    int crescente = 1;
    for (size_t i = 1; i < n; i++) if (v[i - 1] > v[i]) { crescente = 0; break; }
    int preservado = (assinatura(v, n) == ass_antes);

    printf("Elementos: %zu | Ordem crescente: %s | Multiconjunto preservado: %s\n",
           n, crescente ? "OK" : "ERRO", preservado ? "OK" : "ERRO");

    free(v);
    return (crescente && preservado) ? EXIT_SUCCESS : EXIT_FAILURE;
}

#include "tables.h"
#include "params.h"
#include "types.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static const int step[8] = {-8, -7, 1, 9, 8, 7, -1, -9};

U64 knight_att[64];
U64 king_att[64];
U64 pawn_push[2][64];
U64 pawn_att[2][64];
U64 inv_pawn_att[2][64];
U64 ray_att[64][8];
int ray_dir[64][64];
int pst[2][6][64];
int piece_val[6];
HashEntry tt[HASH_SIZE];

typedef struct {
  char magic[8];
  uint32_t hash_size;
  uint32_t entry_size;
  uint32_t reserved;
} TTHeader;

static void init_rays(void) {
  int sq, dir, to;
  for (sq = 0; sq < 64; sq++) {
    for (dir = 0; dir < 8; dir++) {
      U64 r = 0;
      int d = step[dir];
      int f = FILE(sq), rank = RANK(sq);
      if (d == 1 || d == -1) {
        for (to = sq + d; to >= 0 && to < 64 && (to / 8) == rank; to += d) r |= 1ULL << to;
      } else if (d == 8 || d == -8) {
        for (to = sq + d; to >= 0 && to < 64 && (to % 8) == f; to += d) r |= 1ULL << to;
      } else {
        for (to = sq + d; to >= 0 && to < 64; to += d) {
          int df = FILE(to) - f, dr = RANK(to) - rank;
          if (df < -1 || df > 1 || dr < -1 || dr > 1) break;
          r |= 1ULL << to;
          f = FILE(to);
          rank = RANK(to);
        }
      }
      ray_att[sq][dir] = r;
    }
    for (to = 0; to < 64; to++) ray_dir[sq][to] = -1;
    for (dir = 0; dir < 8; dir++) {
      U64 r = ray_att[sq][dir];
      while (r) { to = POP(r); r &= r - 1; ray_dir[sq][to] = dir; }
    }
  }
}

static U64 rand64(void) {
  static U64 s = 0x8a5cd789635d2dffULL;
  s ^= s >> 12;
  s ^= s << 25;
  s ^= s >> 27;
  return s * 2685821657736338717ULL;
}

static U64 zobrist_piece[2][6][64];
static U64 zobrist_side;
static U64 zobrist_ep[8];
static U64 zobrist_castle[4];

static void init_zobrist(void) {
  int c, p, sq;
  for (c = 0; c < 2; c++)
    for (p = 0; p < 6; p++)
      for (sq = 0; sq < 64; sq++)
        zobrist_piece[c][p][sq] = rand64();
  zobrist_side = rand64();
  for (sq = 0; sq < 8; sq++) zobrist_ep[sq] = rand64();
  for (sq = 0; sq < 4; sq++) zobrist_castle[sq] = rand64();
}

U64 tables_compute_key(const Board *b) {
  U64 k = 0;
  int c, p, sq;
  for (c = 0; c < 2; c++)
    for (p = 0; p < 6; p++) {
      U64 bb = b->p[c][p];
      while (bb) { sq = POP(bb); bb &= bb - 1; k ^= zobrist_piece[c][p][sq]; }
    }
  if (b->side == B) k ^= zobrist_side;
  if (b->ep >= 0 && b->ep < 64) k ^= zobrist_ep[FILE(b->ep)];
  if (b->castle & 1) k ^= zobrist_castle[0];
  if (b->castle & 2) k ^= zobrist_castle[1];
  if (b->castle & 4) k ^= zobrist_castle[2];
  if (b->castle & 8) k ^= zobrist_castle[3];
  return k;
}

U64 tables_key_after_null(const Board *b) {
  U64 k = b->key ^ zobrist_side;
  if (b->ep >= 0 && b->ep < 64) k ^= zobrist_ep[FILE(b->ep)];
  return k;
}

int tables_zobrist_ready(void) {
  return zobrist_piece[0][0][1] != 0;
}

void tables_ensure_zobrist(void) {
  if (!tables_zobrist_ready()) init_zobrist();
}

void init_tables(void) {
  int sq, i;
  init_rays();
  init_zobrist();
  for (sq = 0; sq < 64; sq++) {
    U64 k = 0;
    int f = FILE(sq), r = RANK(sq);
    for (i = 0; i < 8; i++) {
      int nr = r + (i < 4 ? (i < 2 ? -1 : 1) : 0);
      int nf = f + (i == 0 || i == 4 ? 0 : (i == 1 || i == 3 || i == 5 ? 1 : -1));
      if (i == 0) nf = f - 1;
      if (i == 1) { nf = f + 1; nr = r - 1; }
      if (i == 2) { nf = f + 1; nr = r; }
      if (i == 3) { nf = f + 1; nr = r + 1; }
      if (i == 4) nr = r + 1;
      if (i == 5) { nf = f - 1; nr = r + 1; }
      if (i == 6) nf = f - 1;
      if (i == 7) { nf = f - 1; nr = r - 1; }
      if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) k |= 1ULL << SQ(nf, nr);
    }
    king_att[sq] = k;
    k = 0;
    int dn[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for (i = 0; i < 8; i++) {
      int nf = f + dn[i][0], nr = r + dn[i][1];
      if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) k |= 1ULL << SQ(nf, nr);
    }
    knight_att[sq] = k;
    pawn_push[W][sq] = (r < 7) ? (1ULL << (sq + 8)) : 0;
    if (r == 1) pawn_push[W][sq] |= 1ULL << (sq + 16);
    pawn_push[B][sq] = (r > 0) ? (1ULL << (sq - 8)) : 0;
    if (r == 6) pawn_push[B][sq] |= 1ULL << (sq - 16);
    pawn_att[W][sq] = 0;
    if (r < 7) {
      if (f > 0) pawn_att[W][sq] |= 1ULL << (sq + 7);
      if (f < 7) pawn_att[W][sq] |= 1ULL << (sq + 9);
    }
    pawn_att[B][sq] = 0;
    if (r > 0) {
      if (f > 0) pawn_att[B][sq] |= 1ULL << (sq - 9);
      if (f < 7) pawn_att[B][sq] |= 1ULL << (sq - 7);
    }
    inv_pawn_att[W][sq] = (r >= 1 && f >= 1 ? (1ULL << (sq - 7)) : 0) | (r >= 1 && f <= 6 ? (1ULL << (sq - 9)) : 0);
    inv_pawn_att[B][sq] = (r <= 6 && f <= 6 ? (1ULL << (sq + 7)) : 0) | (r <= 6 && f >= 1 ? (1ULL << (sq + 9)) : 0);
  }
  piece_val[P] = PARAM_VAL_PAWN;
  piece_val[N] = PARAM_VAL_KNIGHT;
  piece_val[BISHOP] = PARAM_VAL_BISHOP;
  piece_val[R] = PARAM_VAL_ROOK;
  piece_val[Q] = PARAM_VAL_QUEEN;
  piece_val[K] = PARAM_VAL_KING;
  for (sq = 0; sq < 64; sq++) {
    int r = RANK(sq), f = FILE(sq);
    int cr = r < 4 ? r : 7 - r;
    int cf = f < 4 ? f : 7 - f;
    int c = cr + cf;
    pst[W][P][sq] = (r >= 1 && r <= 5) ? (r - 1) * 8 + (f >= 2 && f <= 5 ? 10 : 0) : (r == 6 ? 50 : 0);
    pst[B][P][sq] = pst[W][P][63 - sq];
    pst[W][N][sq] = c * 5 + (r >= 2 && r <= 5 && f >= 2 && f <= 5 ? 15 : 0);
    pst[B][N][sq] = pst[W][N][63 - sq];
    pst[W][BISHOP][sq] = c * 4 + (f == r || f == 7 - r ? 12 : 0);
    pst[B][BISHOP][sq] = pst[W][BISHOP][63 - sq];
    pst[W][R][sq] = (r == 6 ? 25 : 0) + (f == 0 || f == 7 ? -5 : 0) + (r == 7 ? 12 : 0);
    pst[B][R][sq] = pst[W][R][63 - sq];
    pst[W][Q][sq] = c * 3 + (r >= 2 && r <= 5 ? 8 : 0);
    pst[B][Q][sq] = pst[W][Q][63 - sq];
    pst[W][K][sq] = (r == 0 && f >= 2 && f <= 6 ? -30 : 0) + (r >= 1 ? (c * 4) : 0);
    pst[B][K][sq] = pst[W][K][63 - sq];
  }
}

void tt_clear(void) {
  memset(tt, 0, sizeof(tt));
}

static int tt_header_ok(const TTHeader *h) {
  if (memcmp(h->magic, "TTv1", 4) != 0) return 0;
  if (h->hash_size != HASH_SIZE) return 0;
  if (h->entry_size != (uint32_t)sizeof(HashEntry)) return 0;
  return 1;
}

int tt_load(const char *path) {
  if (!path || !*path) return 0;
  FILE *f = fopen(path, "rb");
  if (!f) { tt_clear(); return 0; }
  TTHeader h;
  if (fread(&h, sizeof(h), 1, f) != 1) { fclose(f); tt_clear(); return 0; }
  if (!tt_header_ok(&h)) { fclose(f); tt_clear(); return 0; }
  size_t n = fread(tt, sizeof(HashEntry), HASH_SIZE, f);
  fclose(f);
  if (n != HASH_SIZE) {
    tt_clear();
    return 0;
  }
  return 1;
}

int tt_save(const char *path) {
  if (!path || !*path) return 0;
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  TTHeader h;
  memset(&h, 0, sizeof(h));
  memcpy(h.magic, "TTv1", 4);
  h.hash_size = HASH_SIZE;
  h.entry_size = (uint32_t)sizeof(HashEntry);
  if (fwrite(&h, sizeof(h), 1, f) != 1) { fclose(f); return 0; }
  size_t n = fwrite(tt, sizeof(HashEntry), HASH_SIZE, f);
  fclose(f);
  return n == HASH_SIZE;
}

#ifndef PARAMS_H
#define PARAMS_H

extern const int PARAM_QMAX;
extern const int PARAM_NULL_DEPTH;
extern const int PARAM_LMR_DEPTH;
extern const int PARAM_LMR_MOVES;
extern const int PARAM_ASPIRATION_DELTA;
extern const int PARAM_ASPIRATION_GROW;
extern const int PARAM_MATE_WINDOW_MARGIN;
extern const int PARAM_MATE_SCORE_CUTOFF;
extern const int PARAM_HASH_MOVE_SCORE;
extern const int PARAM_HASH_MOVE_TOP_SCORE;
extern const int PARAM_PROMO_BASE_SCORE;
extern const int PARAM_CAPTURE_BASE_SCORE;
extern const int PARAM_CAPTURE_MVV_LVA_FACTOR;
extern const int PARAM_KILLER_SCORE_1;
extern const int PARAM_KILLER_SCORE_2;
extern const int PARAM_HISTORY_MAX;
extern const int PARAM_NULL_REDUCTION;
extern const int PARAM_LMR_REDUCTION;
extern const int PARAM_MATE_SCORE_WINDOW;
extern const int PARAM_RAZOR_MARGIN;
extern const int PARAM_FUTILITY_MARGIN;
extern const int PARAM_LMP_DEPTH;
extern const int PARAM_LMP_MOVES;

extern const int PARAM_PHASE_MAX;
extern const int PARAM_PHASE_PAWN;
extern const int PARAM_PHASE_KNIGHT;
extern const int PARAM_PHASE_BISHOP;
extern const int PARAM_PHASE_ROOK;
extern const int PARAM_PHASE_QUEEN;

extern const int PARAM_VAL_PAWN;
extern const int PARAM_VAL_KNIGHT;
extern const int PARAM_VAL_BISHOP;
extern const int PARAM_VAL_ROOK;
extern const int PARAM_VAL_QUEEN;
extern const int PARAM_VAL_KING;

extern const int PARAM_PAWN_DOUBLED_PENALTY;
extern const int PARAM_PAWN_ISOLATED_PENALTY;
extern const int PARAM_PASSED_PAWN_BASE;
extern const int PARAM_PASSED_PAWN_ADVANCE;
extern const int PARAM_KING_SHIELD_RANK1;
extern const int PARAM_KING_SHIELD_RANK2;
extern const int PARAM_KING_OPEN_FILE_PENALTY;
extern const int PARAM_KING_BACK_RANK_PENALTY;
extern const int PARAM_ROOK_OPEN_FILE_BONUS;
extern const int PARAM_ROOK_SEMI_OPEN_BONUS;
extern const int PARAM_ROOK_SEVENTH_BONUS;
extern const int PARAM_PAWN_STRUCTURE_WEIGHT;
extern const int PARAM_KING_SAFETY_WEIGHT;
extern const int PARAM_ROOK_ACTIVITY_WEIGHT;
extern const int PARAM_BISHOP_PAIR_BONUS;
extern const int PARAM_KING_ATTACK_SCALE;
extern const int PARAM_ATTACK_WEIGHT_PAWN;
extern const int PARAM_ATTACK_WEIGHT_MINOR;
extern const int PARAM_ATTACK_WEIGHT_ROOK;
extern const int PARAM_ATTACK_WEIGHT_QUEEN;
extern const int PARAM_HANGING_PENALTY_PCT;
extern const int PARAM_TEMPO_BONUS;
extern const int PARAM_MOBILITY_WEIGHT;
extern const int PARAM_DEFAULT_SEARCH_DEPTH;
extern const int PARAM_DEFAULT_MOVE_TIME_MS;
extern const int PARAM_MOVE_TIME_INCREMENT_MS;
extern const int PARAM_TT_CLEAR_ON_NEW_SEARCH;
extern const int PARAM_TT_LOAD_ON_START;
extern const int PARAM_TT_SAVE_ON_EXIT;
extern const char *PARAM_TT_CACHE_PATH;

#endif

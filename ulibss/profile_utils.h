#include <unistd.h>

// StorStack Time Recorder
// #define TIME_REC

void init_time_rec();

void time_rec_add();

void time_rec_newline();

void time_rec_print();

void time_rec_savefile();

void time_rec_savefile_all();

#ifdef TIME_REC

#define INIT_TIME_REC       init_time_rec()
#define TIME_REC_ADD        time_rec_add()
#define TIME_REC_NEWLN      time_rec_newline()
#define TIME_REC_PRINT      time_rec_print()
#define TIME_REC_SAVE       time_rec_savefile()
#define TIME_REC_SAVE_ALL   time_rec_savefile_all()

#else

#define INIT_TIME_REC       ;
#define TIME_REC_ADD        ;
#define TIME_REC_NEWLN      ;
#define TIME_REC_PRINT      ;
#define TIME_REC_SAVE       ;
#define TIME_REC_SAVE_ALL   ;

#endif
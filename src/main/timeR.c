#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// FIXME: s/mbindex/blockidx/ for clarity

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "timeR.h"

/* names of the static bins
 * must be in sync with tr_bin_id_t in timeR.h!
 */
static char *bin_names[TR_FirstUserBin] = {
    // internal
    "Startup",
    "TimingOverhead",
    "UserFuncFallback",

    // memory.c
    "cons",
    "allocVector",
    "allocList",
    "allocS4",
    "GCInternal",
    "Protect",
    "UnprotectPtr",

    // arith.c
    "doArith",

    // connections.c
    "gzFile",
    "bzFile",
    "xzFile",

    // context.c
    "onExits",

    // dotcode.c
    "dotExternal",
    "dotCallFull",
    "dotCall",
    "dotCodeFull",
    "dotCode",

    // dounzip.c
    "doUnzip",
    "zipRead",

    // duplicate.c
    "Duplicate",

    // envir.c
    "SymLookup",
    "FunLookup",
    "FunLookupEval",

    // errors.c
    "CheckStack",

    // eval.c
    "Match",
    "dotBuiltIn",
    "Eval",

    // internet.c
    "Download",

    // logic.c
    "doLogic",

    // main.c
    "Repl",
    "Mainloop",

    // names.c
    "Install",
    "dotSpecial2",

    // subset.c
    "doSubset",
    "doSubset2",

    // Rsock.c
    "inSockRead",
    "inSockWrite",
    "inSockOpen",
    "inSockConnect",

    // sys-std.c
    "Sleep",

    // sys-unix.c
    "System",
};

/* fallback name if string duplication fails */
static char *userfunc_unknown_str = "unknown_user_function";

/* timer list */
static unsigned int max_mbindex;
tr_timer_t  *timeR_measureblocks[TIME_R_MAX_MBLOCKS];
unsigned int timeR_current_mblockidx;
tr_timer_t  *timeR_current_mblock;
unsigned int timeR_next_mindex; // always points to a free timer entry

/* bins */
static unsigned int next_bin = TR_FirstUserBin;
static unsigned int bin_count;
tr_bin_t *timeR_bins;  // is realloc()'d, no pointers to elements please!

/* additional hardcoded timers */
static tr_measureptr_t startup_mptr;


/*** internal functions ***/

static void timeR_print_bin(FILE *fd, const tr_bin_id_t id) {
    fprintf(fd, "%s,%lld,%lld,%llu,%llu\n",
            timeR_bins[id].name,
            timeR_to_nsec(timeR_bins[id].sum_exclusive),
            timeR_to_nsec(timeR_bins[id].sum_complete),
            timeR_bins[id].starts,
            timeR_bins[id].aborts);
}


/*** exported functions ***/

void timeR_init_early(void) {
    unsigned int i;

    /* slightly ugly, but this is called before any part of R is initialized */
    if (!rtime_check_working()) {
	fprintf(stderr, "ERROR: The chosen timing method does not work!\n");
	exit(2);
    }

    /* allocate the first set of timer blocks */
    timeR_measureblocks[0] = calloc(TIME_R_MBLOCK_SIZE, sizeof(tr_timer_t));
    if (timeR_measureblocks[0] == NULL) {
	fprintf(stderr, "ERROR: Failed to allocate the first block of timers!\n");
	exit(2);
    }

    timeR_current_mblock  = timeR_measureblocks[0];
    timeR_current_mblockidx = 0;
    timeR_next_mindex = 1; // the very first timer is just a canary

    /* initialize static bins */
    timeR_bins = calloc(TR_FirstUserBin + TIME_R_INITIAL_USERFUNC_BINS, sizeof(tr_bin_t));
    if (timeR_bins == NULL) {
	fprintf(stderr, "ERROR: Failed to allocate the timing bins!\n");
	exit(2);
    }

    bin_count = TR_FirstUserBin + TIME_R_INITIAL_USERFUNC_BINS;

    for (i = 0; i < next_bin; i++) {
	timeR_bins[i].name = bin_names[i];
    }

    // FIXME: Add a generic init of drop?
    timeR_bins[TR_inSockRead].drop    = 1;
    timeR_bins[TR_inSockWrite].drop   = 1;
    timeR_bins[TR_inSockOpen].drop    = 1;
    timeR_bins[TR_inSockConnect].drop = 1;

    /* measure the startup time of R */
    startup_mptr = timeR_begin_timer(TR_Startup);
}

void timeR_startup_done(void) {
    timeR_end_timer(&startup_mptr);
}

void timeR_finish(void) {
    if (timeR_current_mblockidx != 0 || timeR_next_mindex != 1) {
	/* manually build a mptr to the first timer */
	tr_measureptr_t fini = {
	    timeR_measureblocks[0], 1 // timer 0 is a canary value
	};

	/* end every remaining timer */
	timeR_end_timer(&fini);
    }

    /* dump data to file */
    FILE *fd;

    if (timeR_output_file == NULL)
	return;

    fd = fopen(timeR_output_file, "w");
    if (fd == NULL)
	return;

    timeR_dump(fd);

    // FIXME: Check for errors
    fclose(fd);
}

void timeR_dump(FILE *fd) {
    // FIXME: Check for errors
    fprintf(fd, "name,exclusive,complete,starts,aborts\n");

    for (unsigned int i = 0; i < next_bin; i++) {
	timeR_print_bin(fd, i);
    }
}

void timeR_measureblock_full(void) {
    tr_measureptr_t mptr;

    /* current block is now full, switch to the next one */
    // FIXME: Slow path, add overhead timer here
    timeR_current_mblockidx++;

    /* check for overflow */
    if (timeR_current_mblockidx == TIME_R_MAX_MBLOCKS) {
        /* abort here - timeR_measureblocks can't be realloced because all
           existing tr_measureptr_t reference into it */
        fprintf(stderr, "ERROR: Too many timers allocated!\n"
                "increase TIME_R_MAX_MBLOCKS and recompile\n");
        abort();
    }

    /* allocate a new block if neccessary */
    if (timeR_measureblocks[timeR_current_mblockidx] == NULL) {
        timeR_measureblocks[timeR_current_mblockidx] = malloc(sizeof(tr_timer_t) * TIME_R_MBLOCK_SIZE);
        if (timeR_measureblocks[timeR_current_mblockidx] == NULL) {
            fprintf(stderr, "ERROR: Timer block allocation failed!\n");
            abort();
        }
        max_mbindex = timeR_current_mblockidx;
    }

    timeR_current_mblock = timeR_measureblocks[timeR_current_mblockidx];
    timeR_next_mindex    = 0;
}

void timeR_end_timers_slowpath(const tr_measureptr_t *mptr, timeR_t when,
                               timeR_t prev_diff, timeR_t prev_drop) {
    tr_timer_t *m;
    // FIXME: Add overhead timer

    /* loop until the passed timer has been processed */
    do {
        timeR_end_latest_timer(when, &prev_diff, &prev_drop);

	/* update abort counter if this isn't the top timer */
	if (timeR_current_mblock != mptr->curblock ||
	    timeR_next_mindex    != mptr->index)
            timeR_bins[timeR_current_mblock[timeR_next_mindex].bin_id].aborts++;

    } while (timeR_current_mblock != mptr->curblock ||
             timeR_next_mindex    != mptr->index);

    /* add last difference in the top-level active timer */
    if (timeR_next_mindex == 0) {
	m = &timeR_measureblocks[timeR_current_mblockidx - 1][TIME_R_MBLOCK_SIZE - 1];
    } else {
	m = &timeR_current_mblock[timeR_next_mindex-1];
    }

    m->lower_sum += prev_diff;
}

unsigned int timeR_add_userfn_bin(void) {
    /* check if there are bins available */
    if (next_bin >= bin_count) {
        // FIXME: consider switching to (limited?) exponential resizes
	tr_bin_t *newbins =
	    realloc(timeR_bins, (bin_count + TIME_R_INITIAL_USERFUNC_BINS) *
	                  sizeof(tr_bin_t));

	if (newbins == NULL)
	    /* realloc failed, return the fallback */
	    return TR_UserFuncFallback;

	/* clear the new entries */
	memset(newbins + bin_count, 0, sizeof(tr_bin_t) * TIME_R_INITIAL_USERFUNC_BINS);

	/* update bookkeeping */
	timeR_bins = newbins;
	bin_count += TIME_R_INITIAL_USERFUNC_BINS;
    }

    return next_bin++;
}

void timeR_name_bin(unsigned int bin_id, char *name) {
    /* create permanent copy of name */
    char *copy = strdup(name);
    if (!copy)
        copy = userfunc_unknown_str;

    if (timeR_bins[bin_id].name != NULL)
        free(timeR_bins[bin_id].name);

    timeR_bins[bin_id].name = copy;
}

/* set a bin name for an anonymous function */
void timeR_name_bin_anonfunc(unsigned int bin_id, const char *filename,
                             unsigned int line, unsigned int pos) {
  char nametmp[1024];

  nametmp[sizeof(nametmp) - 1] = 0;
  snprintf(nametmp, sizeof(nametmp), "%s:<anon function defined in line %d column %d>",
           filename, line, pos);
  timeR_name_bin(bin_id, nametmp);
}

/* explicitly stop timers if a SETJMP returns */
/* This function is not inlined because it is assumed that */
/* it will only rarely be called.                          */
void timeR_release(tr_measureptr_t *marker) {
    /* check if anything needs to be done at all */
    if (marker->curblock == timeR_current_mblock &&
        marker->index    == timeR_next_mindex)
        return;

    /* marker points to an allocated measurement, end it */
    timeR_end_timer(marker);
}

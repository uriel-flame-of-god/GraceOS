// ============================
// GraceOS Shell History
// Command history management
// ============================

#include "history.h"
#include "../../lib/libc/string.h"

/* History buffer */
static char hist[HIST_MAX][HIST_LEN];
static int head = 0;        /* Next write position */
static int count = 0;       /* Number of entries */
static int pos = 0;         /* Current browse position */
static int browsing = 0;    /* Are we currently browsing? */

/* Initialize history */
void hist_init(void)
{
    head = 0;
    count = 0;
    pos = 0;
    browsing = 0;
    
    /* Clear all entries */
    for (int i = 0; i < HIST_MAX; i++)
    {
        hist[i][0] = '\0';
    }
}

/* Add command to history */
void hist_add(const char* cmd)
{
    /* Don't add empty commands */
    if (!cmd || cmd[0] == '\0')
        return;
    
    /* Don't add duplicates of last command */
    if (count > 0)
    {
        int last = (head - 1 + HIST_MAX) % HIST_MAX;
        if (strcmp(cmd, hist[last]) == 0)
            return;
    }
    
    /* Copy command to history */
    strncpy(hist[head], cmd, HIST_LEN - 1);
    hist[head][HIST_LEN - 1] = '\0';
    
    /* Advance head */
    head = (head + 1) % HIST_MAX;
    
    /* Update count */
    if (count < HIST_MAX)
        count++;
    
    /* Reset browse position */
    pos = head;
    browsing = 0;
}

/* Get previous command */
const char* hist_prev(void)
{
    if (count == 0)
        return 0;
    
    if (!browsing)
    {
        /* Start browsing from most recent */
        pos = (head - 1 + HIST_MAX) % HIST_MAX;
        browsing = 1;
        return hist[pos];
    }
    
    /* Calculate oldest valid entry */
    int oldest = (head - count + HIST_MAX) % HIST_MAX;
    
    /* Check if we can go further back */
    int prev = (pos - 1 + HIST_MAX) % HIST_MAX;
    
    /* Don't go past oldest entry */
    if (pos == oldest)
        return hist[pos];  /* Return current, can't go further */
    
    pos = prev;
    return hist[pos];
}

/* Get next command */
const char* hist_next(void)
{
    if (count == 0 || !browsing)
        return 0;
    
    /* Most recent is one before head */
    int newest = (head - 1 + HIST_MAX) % HIST_MAX;
    
    /* Check if we're at the newest */
    if (pos == newest)
    {
        browsing = 0;
        return 0;  /* Signal to restore original input */
    }
    
    pos = (pos + 1) % HIST_MAX;
    return hist[pos];
}

/* Get command by index */
const char* hist_get(int index)
{
    if (index < 0 || index >= count)
        return 0;
    
    /* Index 0 = oldest entry */
    int oldest = (head - count + HIST_MAX) % HIST_MAX;
    int actual = (oldest + index) % HIST_MAX;
    
    return hist[actual];
}

/* Get history count */
int hist_count(void)
{
    return count;
}

/* Reset browsing position */
void hist_reset_pos(void)
{
    pos = head;
    browsing = 0;
}

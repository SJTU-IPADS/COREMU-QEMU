/* Included in softmmu_header.h */

/* Because of multiple including in other files, I'm not able to make these
 * inline functions. */

#define READ_WITH_ID(res, hostaddr, objid, restype) \
    do { \
        unsigned long __hostaddr = hostaddr; \
        if (!cm_is_in_tc) \
            res = (restype)*(DATA_TYPE *)(__hostaddr); \
        else if (cm_run_mode == CM_RUNMODE_RECORD) \
            res = (restype)glue(cm_crew_record_read, SUFFIX)((DATA_TYPE *)(__hostaddr), objid); \
        else \
            res = (restype)glue(cm_crew_replay_read, SUFFIX)((DATA_TYPE *)(__hostaddr), objid); \
    } while (0)

#define WRITE_WITH_ID(hostaddr, objid, v) \
    do { \
        unsigned long __hostaddr = hostaddr; \
        if (!cm_is_in_tc) \
            *(DATA_TYPE *)(__hostaddr) = v; \
        else if (cm_run_mode == CM_RUNMODE_RECORD) \
            glue(cm_crew_record_write, SUFFIX)((DATA_TYPE *)(__hostaddr), \
                objid, v); \
        else \
            glue(cm_crew_replay_write, SUFFIX)((DATA_TYPE *)(__hostaddr), objid, v); \
    } while (0)


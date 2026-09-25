#ifndef GE_SIMULANT_PROBE_H
#define GE_SIMULANT_PROBE_H

struct PropRecord;
struct PropRecord *simulantProbeGetProp(void);

/* Temporary PC-only proof that an AI character can exist beside MP players. */
void simulantProbePoll(void);
/* Allow the probe to use stage data only after lvlStageLoad has returned. */
void simulantProbeStageReady(int stage);
/* Called after stage guard/object cleanup, before MEMPOOL_STAGE is reused. */
void simulantProbeStageTeardown(void);

#endif

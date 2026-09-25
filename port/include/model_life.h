#ifndef GE_PORT_MODEL_LIFE_H
#define GE_PORT_MODEL_LIFE_H

/* PC-only, GE_MODEL_LIFE=1 diagnostic. Never changes the resource cache. */
struct ModelFileHeader;

void modelLifeStageBegin(int stage);
void modelLifeStageEnd(int stage);
void modelLifeStageCleanup(void);
void modelLifeEvent(const char *event, struct ModelFileHeader *header);
void modelLifeLoaded(struct ModelFileHeader *header, const char *name, void *filedata,
                     int explicitDestination);
void modelLifeValidate(const char *event, struct ModelFileHeader *header);
void modelLifeBot(int body, int head);
void modelLifeAttachment(const char *event, struct ModelFileHeader *body,
                         struct ModelFileHeader *head, void *placeholder,
                         void *child, void *parent);

/* Opt-in debugger handoff: GE_MODEL_WATCH=<resource filename>.
 * The breakpoint is before promotion, so the debugger can watch both the
 * expected relocation and every later write to the selected root node. */
void modelLifeWatchCheckpoint(const char *phase, const char *name,
                              struct ModelFileHeader *header);
void modelLifeWatchBreak(void);

#endif

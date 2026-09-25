#ifndef GE_MP_SIMULANTS_H
#define GE_MP_SIMULANTS_H

/* Setup configuration is separate from the stage-owned participant roster. */
int mpSimulantsGetCount(void);
void mpSimulantsSetCount(int humans, int count);
int mpSimulantsIsMatch(void);
int mpSimulantsCanStart(int humans, int inputs);

#endif

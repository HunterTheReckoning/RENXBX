/*
** animatedsoundmgr_xbox.cpp -- placeholder for AnimatedSoundMgrClass on the Xbox (nxdk only).
**
** The real animatedsoundmgr.cpp plays sounds embedded in animations (footsteps and the
** like) through WWAudio, which needs the Miles sound library that EA's source release
** leaves out. Until the port has its own audio system, this provides the same interface and
** behaves exactly like the original with no sounds loaded: no animation has embedded sounds,
** and Trigger_Sound returns old_frame unchanged, as the original does when nothing plays.
**
** Remove this file from the Xbox build when animatedsoundmgr.cpp is ported.
*/
#include "animatedsoundmgr.h"

#ifdef NXDK

#include "wwdebug.h"

void AnimatedSoundMgrClass::Initialize(const char *)
{
	WWDEBUG_SAY(("AnimatedSoundMgrClass: animation sounds are not available yet (audio not ported)\n"));
}

void AnimatedSoundMgrClass::Shutdown(void)
{
}

bool AnimatedSoundMgrClass::Does_Animation_Have_Embedded_Sounds(HAnimClass *)
{
	return false;
}

float AnimatedSoundMgrClass::Trigger_Sound(HAnimClass *, float old_frame, float, const Matrix3D &)
{
	return old_frame;
}

#endif /* NXDK */

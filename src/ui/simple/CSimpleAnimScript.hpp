#ifndef UI_SIMPLE_C_SIMPLE_ANIM_SCRIPT_HPP
#define UI_SIMPLE_C_SIMPLE_ANIM_SCRIPT_HPP

#include "ui/FrameScript.hpp"

// Both counts and both orderings are the reference's, read out of its method tables at 00ac18e0
// and 00ac1ab8. Keep the order: frozen's other widget tables mirror the reference's exactly, and
// the recomp matcher pairs tables positionally.
//
// Animation's table deliberately does NOT carry GetObjectType / IsObjectType / GetName /
// GetParent, where AnimationGroup's does. That asymmetry is the reference's, not an omission here.
#define NUM_SIMPLE_ANIM_SCRIPT_METHODS 31
#define NUM_SIMPLE_ANIM_GROUP_SCRIPT_METHODS 26

extern FrameScript_Method SimpleAnimMethods[NUM_SIMPLE_ANIM_SCRIPT_METHODS];
extern FrameScript_Method SimpleAnimGroupMethods[NUM_SIMPLE_ANIM_GROUP_SCRIPT_METHODS];

#endif

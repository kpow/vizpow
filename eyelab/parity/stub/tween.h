// Host stand-in for vizbot/tween.h: bot_faces.h declares tweens the eye model never runs.
#pragma once
enum { EASE_OUT_QUAD = 1 };
struct TweenMgr { void start(float*, float, float, uint16_t, int) {} };
static TweenMgr tweenManager;

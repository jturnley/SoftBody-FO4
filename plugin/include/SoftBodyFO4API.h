#pragma once
// SoftBodyFO4's interface for other F4SE plugins (4Strands): the vaginal canal, cervix and uterus of the woman in the
// 4Stim scene SoftBodyFO4 follows, as it places them, and requests back (how full the uterus is, how open the os).
//
// Load it with GetModuleHandleW(L"SoftBodyFO4.dll") and GetProcAddress for each function; all of them are safe to call
// from any thread at any time. Positions are in world space (game units), as of SoftBodyFO4's last placement (every
// few milliseconds while a scene runs).
#include <cstdint>

namespace SoftBodyFO4API
{
	constexpr std::uint32_t kVersion = 1;
	constexpr std::uint32_t kMaxRings = 8;

	struct Ring
	{
		float centre[3];
		float radius;  // how far open it is now (its rest radius when nothing is in it)
	};

	struct Canal
	{
		std::uint32_t version;      // kVersion
		std::uint32_t herFormID;    // 0: no scene followed, or her canal isn't built (everything below is then unset)
		std::uint32_t ringCount;    // the canal's rings, entrance first, deepest last (6)
		Ring          rings[kMaxRings];
		float         cervix[3];    // the canal's closed end (where his tip meets it), with the push applied
		Ring          os;           // the os: the cervical canal's middle and how open it is
		float         uterusCentre[3];
		float         uterusAxis[3];  // unit, from the os toward the fundus
		float         uterusLength;   // os to fundus
		float         uterusRadius;   // side to side, at its middle (front to back is about half)
		float         fill;           // as last set (0-1)
		float         cervixPush;     // how far his tip carries the cervix deeper now
	};
}

// Every function is extern "C"; the typedefs are for GetProcAddress.
// bool SoftBodyFO4_GetCanal(SoftBodyFO4API::Canal* out): fills `out`; false if there is nothing to report.
using SoftBodyFO4_GetCanal_t = bool (*)(SoftBodyFO4API::Canal*);
// void SoftBodyFO4_SetUterusFill(std::uint32_t herFormID, float fill): 0 empty to 1 full; the uterus swells with it
// (fUterusSwell in SoftBodyFO4.ini). Ignored for anyone but the woman followed. Kept until set again or the scene ends.
using SoftBodyFO4_SetUterusFill_t = void (*)(std::uint32_t, float);
// void SoftBodyFO4_SetOsOpen(std::uint32_t herFormID, float radius): the os opens to this (game units; 0 back to its
// rest), e.g. while fluid passes through it.
using SoftBodyFO4_SetOsOpen_t = void (*)(std::uint32_t, float);

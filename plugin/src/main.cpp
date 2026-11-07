// SoftBodyFO4: SoftBody FO4's F4SE plugin. In each 4Stim scene it
//  1. gives her body the physics for the scene's act, as GT Softbody switches its collision by act: for vaginal sex
//     SoftBody-CBBE-Vaginal.xml, for anal -Anal.xml, otherwise the full SoftBody-CBBE.xml (FO4 FSMP's DynamicHDT).
//     The opening not in use lets the penis through, so it can't catch it or be dragged about by it.
//     The act comes from 4Stim's Scenes interface; a 4Stim without it gets it from where the penis goes instead.
//  2. aligns the penis with the opening in use. GT gets there with a bendable penis that her collision steers in;
//     4Stim's penis is rigid, so his 4Stim alignment offset is corrected instead (4Stim saves it with the scene):
//     where the penis line crosses the opening is measured over a couple of seconds of the loop, he is moved, and it
//     is measured again, until it is within the tolerance. Then, if the penis runs at an angle to the canal (missionary:
//     about 20 degrees), 4Stim's penis bend is tried a step or two to learn its effect, set to match, and the entrance
//     centred again.
//  3. places the openings' rims round the penis (their bones kinematic in her physics files), every few ms.
//  4. optionally logs her genital bones and the penis (what was SoftBodyProbe), for tuning.
// Settings: Data\F4SE\Plugins\SoftBodyFO4.ini. Keys, with the game in front: F11 steps the alignment through auto,
// the vagina, the anus (either with that act's physics) and off, aligning at once; Ctrl+F11 goes back to the
// scene's own alignment.

#include "PluginInterface.h"
#include "../include/SoftBodyFO4API.h"

extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int);
extern "C" __declspec(dllimport) void* __stdcall GetForegroundWindow();
extern "C" __declspec(dllimport) unsigned long __stdcall GetWindowThreadProcessId(void*, unsigned long*);
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentProcessId();

namespace
{
	using Clock = std::chrono::steady_clock;
	using V = RE::NiPoint3;

	// ---------------------------------------------------------------- settings

	namespace Settings
	{
		bool  swapPhysics = true;
		bool  autoAlign = true;
		bool  logSamples = false;
		int   alignKey = 0x7A;  // F11
		float tolerance = 0.4F;  // game units: the penis line within this of the opening's centre is aligned
		int   maxSteps = 5;
		float maxStep = 6.0F;  // the most he is moved in one step
		// the openings' rims, placed round the penis (game units, at his scale 1)
		bool  driveOpenings = true;
		float penisRadius = 2.2F;     // the shaft's at its root (A-Body's: 2.2)
		float penisRadiusTip = 1.8F;  // and at its head end (A-Body's: 1.8 two thirds along)
		float rimMargin = 0.15F;      // how far clear of him the hole opens
		bool  bendPenis = true;       // turn his penis at its root to point through the opening in use
		float bendMax = 20.0F;        // degrees, at most
		float bendSmoothing = 0.1F;   // seconds: how quickly the bend follows
		float penisTipLength = 1.5F;  // the rounded tip: the radius grows from 0 at its end to the shaft's over this
		float penisTipExtra = 0.75F;  // the penis's end beyond its last bone (Penis_05_SMP; A-Body's: 0.73)
		float penisBaseExtra = 4.0F;  // the shaft carried on behind its first bone (Penis_01_SMP), into his body, so
		                              // a rim at the root, fully in, stays open round it
		float rimSmoothing = 0.02F;   // seconds: how quickly the rims follow
		float rimEdge = 0.5F;         // an opening's edge at rest, from its centre: where a rim bone's skin starts
		float rimFollow = 1.5F;       // how far an opening's ring moves across onto the penis's line
		float rimCatch = 1.0F;        // how far past his thickness his line may pass an opening's centre and still open it
		bool  driveCanal = true;      // centre the vaginal canal's rings on his line and open them to his thickness
		float canalFollow = 8.0F;     // how far a canal ring moves across onto his line
		float canalCatch = 8.0F;      // how far past his thickness his line may pass a canal ring's centre and still open it
		float cervixStretch = 4.0F;   // how far his tip may carry the cervix deeper
		float anusAimBack = 0.0F;
		bool  calmBalls = true;       // v0.40: hold his balls' swing within ballMax of where they hang, smoothed
		float ballMax = 20.0F;        // degrees
		float ballSmoothing = 0.08F;  // seconds
		float ballStretch = 0.3F;     // units: the most a ball may move from where it hangs (v0.41)
		float snakeMax = 60.0F;       // degrees: the most her canal may turn from his line and still be followed     // the anus's aim moved this far toward her back (onto its hole, as seen in game)
		float uterusSwell = 0.6F;     // how much wider the uterus grows when full (0.6: by 60%)

		void Load()
		{
			const auto path = (std::filesystem::current_path() / "Data" / "F4SE" / "Plugins" / "SoftBodyFO4.ini").string();
			const char* ini = path.c_str();
			auto i = [&](const char* a_key, int a_default) {
				return static_cast<int>(REX::W32::GetPrivateProfileIntA("General", a_key, a_default, ini));
			};
			auto f = [&](const char* a_key, float a_default) {
				char buf[32]{};
				REX::W32::GetPrivateProfileStringA("General", a_key, "", buf, sizeof(buf), ini);
				return buf[0] ? std::strtof(buf, nullptr) : a_default;
			};
			swapPhysics = i("bSwapPhysics", 1) != 0;
			autoAlign = i("bAutoAlign", 1) != 0;  // the mode F11 starts in: auto, or off
			logSamples = i("bLogSamples", 0) != 0;
			alignKey = i("iAlignKey", alignKey);
			tolerance = f("fAlignTolerance", tolerance);
			maxSteps = i("iAlignMaxSteps", maxSteps);
			maxStep = f("fAlignMaxStep", maxStep);
			driveOpenings = i("bDriveOpenings", 1) != 0;
			penisRadius = f("fPenisRadius", penisRadius);
			penisRadiusTip = f("fPenisRadiusTip", penisRadiusTip);
			rimMargin = f("fRimMargin", rimMargin);
			bendPenis = i("bBendPenis", 1) != 0;
			driveCanal = i("bDriveCanal", 1) != 0;
			canalFollow = f("fCanalFollow", canalFollow);
			canalCatch = f("fCanalCatch", canalCatch);
			cervixStretch = f("fCervixStretch", cervixStretch);
			uterusSwell = f("fUterusSwell", uterusSwell);
			anusAimBack = f("fAnusAimBack", anusAimBack);
			snakeMax = f("fSnakeMax", snakeMax);
			calmBalls = i("bCalmBalls", 1) != 0;
			ballMax = f("fBallMax", ballMax);
			ballSmoothing = f("fBallSmoothing", ballSmoothing);
			ballStretch = f("fBallStretch", ballStretch);
			bendMax = std::clamp(f("fBendMax", bendMax), 0.0F, 45.0F);
			bendSmoothing = std::max(0.0F, f("fBendSmoothing", bendSmoothing));
			penisTipLength = std::max(0.1F, f("fPenisTipLength", penisTipLength));
			penisTipExtra = f("fPenisTipExtra", penisTipExtra);
			penisBaseExtra = std::max(0.0F, f("fPenisBaseExtra", penisBaseExtra));
			rimSmoothing = std::max(0.0F, f("fRimSmoothing", rimSmoothing));
			rimEdge = std::max(0.0F, f("fRimEdge", rimEdge));
			rimFollow = std::max(0.0F, f("fRimFollow", rimFollow));
			rimCatch = std::max(0.0F, f("fRimCatch", rimCatch));
			REX::INFO("settings: openings driven {}, penis radius {}, tip length {}, tip beyond its bone {}, rim smoothing {}s",
				driveOpenings, penisRadius, penisTipLength, penisTipExtra, rimSmoothing);
			REX::INFO("settings: swap physics {}, auto-align {}, log samples {}, align key {:#x}, tolerance {}, {} steps of up to {}",
				swapPhysics, autoAlign, logSamples, alignKey, tolerance, maxSteps, maxStep);
		}
	}

	// ---------------------------------------------------------------- small vector maths

	float Dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	V     Cross(const V& a, const V& b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
	V     Mul(const V& a, float s) { return V(a.x * s, a.y * s, a.z * s); }
	V     Add(const V& a, const V& b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
	V     Sub(const V& a, const V& b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
	float Len(const V& a) { return std::sqrt(Dot(a, a)); }
	V     Unit(const V& a)
	{
		const float l = Len(a);
		return l > 1e-6F ? Mul(a, 1.0F / l) : V(0, 0, 0);
	}

	struct M3
	{
		float m[3][3]{};

		V operator*(const V& v) const
		{
			return V(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
				m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
		}
		M3 operator*(const M3& o) const
		{
			M3 r;
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j)
					for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * o.m[k][j];
			return r;
		}
		M3 T() const
		{
			M3 r;
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
			return r;
		}
		M3 Inverse() const
		{
			const auto& a = m;
			const float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
			                  a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
			M3 r;
			if (std::abs(det) < 1e-12F) {
				return r;
			}
			const float k = 1.0F / det;
			r.m[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * k;
			r.m[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * k;
			r.m[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * k;
			r.m[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * k;
			r.m[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * k;
			r.m[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * k;
			r.m[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * k;
			r.m[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * k;
			r.m[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * k;
			return r;
		}
		static M3 Columns(const V& a, const V& b, const V& c)
		{
			M3 r;
			const V cols[3] = { a, b, c };
			for (int j = 0; j < 3; ++j) {
				r.m[0][j] = cols[j].x;
				r.m[1][j] = cols[j].y;
				r.m[2][j] = cols[j].z;
			}
			return r;
		}
	};

	// a world point in a node's own frame. FO4 keeps NiMatrix3 transposed from what NiTransform's operator* assumes
	// (4Strands' BodyProbe found the same with skin-to-bone), so the stored matrix itself maps world to local
	V ToLocal(const RE::NiAVObject* a_node, const V& a_world)
	{
		const auto& w = a_node->world;
		return (w.rotate * (a_world - w.translate)) / w.scale;
	}

	// a point in a node's own frame, in the world (ToLocal's inverse)
	V ToWorld(const RE::NiAVObject* a_node, const V& a_local)
	{
		const auto& w = a_node->world;
		const auto& e = w.rotate.entry;
		const V     s(a_local.x * w.scale, a_local.y * w.scale, a_local.z * w.scale);
		return V(w.translate.x + e[0][0] * s.x + e[1][0] * s.y + e[2][0] * s.z,
			w.translate.y + e[0][1] * s.x + e[1][1] * s.y + e[2][1] * s.z,
			w.translate.z + e[0][2] * s.x + e[1][2] * s.y + e[2][2] * s.z);
	}

	V DirToLocal(const RE::NiAVObject* a_node, const V& a_world)
	{
		const auto& w = a_node->world;
		return (w.rotate * a_world) / w.scale;
	}

	// ---------------------------------------------------------------- what is held and logged

	struct Pair
	{
		const char* bone;
		const char* anchor;
	};

	// each moving bone and the bone its spring hangs from (SoftBody-CBBE.xml)
	constexpr Pair kBones[] = {
		{ "Clitoral1", "Clitoral" }, { "VaginaB1", "VaginaB" }, { "NPC L Pussy02", "NPC L Pussy01" }, { "NPC R Pussy02", "NPC R Pussy01" },
		{ "VaginaDeep1", "VaginaDeep" }, { "VaginaDeep2", "VaginaDeep1" },
		{ "VaginaDB01", "VaginaDB00" }, { "VaginaDF01", "VaginaDF00" }, { "VaginaDL01", "VaginaDL00" }, { "VaginaDR01", "VaginaDR00" },
		{ "VaginaDB02", "VaginaDB00" }, { "VaginaDF02", "VaginaDF00" }, { "VaginaDL02", "VaginaDL00" }, { "VaginaDR02", "VaginaDR00" },
		{ "VaginaDB03", "VaginaDB00" }, { "VaginaDF03", "VaginaDF00" }, { "VaginaDL03", "VaginaDL00" }, { "VaginaDR03", "VaginaDR00" },
		{ "NPC LT Anus2", "NPC LT Anus1" }, { "NPC RT Anus2", "NPC RT Anus1" }, { "NPC LB Anus2", "NPC LB Anus1" }, { "NPC RB Anus2", "NPC RB Anus1" },
		{ "SB AnusT1", "SB AnusT1 Base" }, { "SB AnusB1", "SB AnusB1 Base" }, { "SB AnusL1", "SB AnusL1 Base" }, { "SB AnusR1", "SB AnusR1 Base" },
		{ "SB AnusT2", "SB AnusT2 Base" }, { "SB AnusB2", "SB AnusB2 Base" }, { "SB AnusL2", "SB AnusL2 Base" }, { "SB AnusR2", "SB AnusR2 Base" },
		{ "SB AnusT3", "SB AnusT3 Base" }, { "SB AnusB3", "SB AnusB3 Base" }, { "SB AnusL3", "SB AnusL3 Base" }, { "SB AnusR3", "SB AnusR3 Base" },
		// the vaginal entrance and canal rings: front, left, back, right of each (12 bones a ring)
		{ "SB VagIn_0", "SB VagIn_0 Base" }, { "SB VagIn_3", "SB VagIn_3 Base" }, { "SB VagIn_6", "SB VagIn_6 Base" }, { "SB VagIn_9", "SB VagIn_9 Base" },
		{ "SB Vag1_0", "SB Vag1_0 Base" }, { "SB Vag1_3", "SB Vag1_3 Base" }, { "SB Vag1_6", "SB Vag1_6 Base" }, { "SB Vag1_9", "SB Vag1_9 Base" },
		{ "SB Vag2_0", "SB Vag2_0 Base" }, { "SB Vag2_3", "SB Vag2_3 Base" }, { "SB Vag2_6", "SB Vag2_6 Base" }, { "SB Vag2_9", "SB Vag2_9 Base" },
		{ "SB Vag3_0", "SB Vag3_0 Base" }, { "SB Vag3_3", "SB Vag3_3 Base" }, { "SB Vag3_6", "SB Vag3_6 Base" }, { "SB Vag3_9", "SB Vag3_9 Base" },
		{ "SB Vag4_0", "SB Vag4_0 Base" }, { "SB Vag4_3", "SB Vag4_3 Base" }, { "SB Vag4_6", "SB Vag4_6 Base" }, { "SB Vag4_9", "SB Vag4_9 Base" },
		{ "SB Vag5_0", "SB Vag5_0 Base" }, { "SB Vag5_3", "SB Vag5_3 Base" }, { "SB Vag5_6", "SB Vag5_6 Base" }, { "SB Vag5_9", "SB Vag5_9 Base" },
		// v0.39: her butt and belly, to see whether his body moves them: each region bone from its skeleton bone, each
		// dent from its region bone
		{ "LButtFat_skin_SB", "LButtFat_skin" },
		{ "LButtFat_skin_SB01", "LButtFat_skin_SB" }, { "LButtFat_skin_SB02", "LButtFat_skin_SB" }, { "LButtFat_skin_SB03", "LButtFat_skin_SB" },
		{ "LButtFat_skin_SB04", "LButtFat_skin_SB" }, { "LButtFat_skin_SB05", "LButtFat_skin_SB" }, { "LButtFat_skin_SB06", "LButtFat_skin_SB" },
		{ "LButtFat_skin_SB07", "LButtFat_skin_SB" }, { "LButtFat_skin_SB08", "LButtFat_skin_SB" },
		{ "RButtFat_skin_SB", "RButtFat_skin" },
		{ "RButtFat_skin_SB01", "RButtFat_skin_SB" }, { "RButtFat_skin_SB02", "RButtFat_skin_SB" }, { "RButtFat_skin_SB03", "RButtFat_skin_SB" },
		{ "RButtFat_skin_SB04", "RButtFat_skin_SB" }, { "RButtFat_skin_SB05", "RButtFat_skin_SB" }, { "RButtFat_skin_SB06", "RButtFat_skin_SB" },
		{ "RButtFat_skin_SB07", "RButtFat_skin_SB" }, { "RButtFat_skin_SB08", "RButtFat_skin_SB" },
		{ "Belly_skin_SB", "Belly_skin" },
		{ "Belly_skin_SB01", "Belly_skin_SB" }, { "Belly_skin_SB02", "Belly_skin_SB" }, { "Belly_skin_SB03", "Belly_skin_SB" },
		{ "Belly_skin_SB04", "Belly_skin_SB" }, { "Belly_skin_SB05", "Belly_skin_SB" }, { "Belly_skin_SB06", "Belly_skin_SB" },
		{ "Belly_skin_SB07", "Belly_skin_SB" }, { "Belly_skin_SB08", "Belly_skin_SB" }, { "Belly_skin_SB09", "Belly_skin_SB" },
	};

	struct Width
	{
		const char* name;
		const char* a;
		const char* b;
	};

	constexpr Width kWidths[] = {
		{ "lips_LR", "NPC L Pussy02", "NPC R Pussy02" }, { "lips_FB", "Clitoral1", "VaginaB1" },
		{ "canal1_LR", "VaginaDL01", "VaginaDR01" }, { "canal1_FB", "VaginaDF01", "VaginaDB01" },
		{ "canal2_LR", "VaginaDL02", "VaginaDR02" }, { "canal2_FB", "VaginaDF02", "VaginaDB02" },
		{ "canal3_LR", "VaginaDL03", "VaginaDR03" }, { "canal3_FB", "VaginaDF03", "VaginaDB03" },
		{ "anus_LTRB", "NPC LT Anus2", "NPC RB Anus2" }, { "anus_RTLB", "NPC RT Anus2", "NPC LB Anus2" },
		{ "anal1_TB", "SB AnusT1", "SB AnusB1" }, { "anal1_LR", "SB AnusL1", "SB AnusR1" },
		{ "anal2_TB", "SB AnusT2", "SB AnusB2" }, { "anal2_LR", "SB AnusL2", "SB AnusR2" },
		{ "anal3_TB", "SB AnusT3", "SB AnusB3" }, { "anal3_LR", "SB AnusL3", "SB AnusR3" },
		{ "vagRim_FB", "SB VagRim_0", "SB VagRim_12" }, { "vagRim_LR", "SB VagRim_6", "SB VagRim_18" },
		{ "anusRim_TB", "SB AnusRim_0", "SB AnusRim_12" }, { "anusRim_LR", "SB AnusRim_6", "SB AnusRim_18" },
		{ "vagRimO_FB", "SB VagRimO_0", "SB VagRimO_6" }, { "vagRimO_LR", "SB VagRimO_3", "SB VagRimO_9" },
		{ "anusRimO_TB", "SB AnusRimO_0", "SB AnusRimO_6" }, { "anusRimO_LR", "SB AnusRimO_3", "SB AnusRimO_9" },
		{ "vagIn_FB", "SB VagIn_0", "SB VagIn_6" }, { "vagIn_LR", "SB VagIn_3", "SB VagIn_9" },
		{ "vag1_FB", "SB Vag1_0", "SB Vag1_6" }, { "vag1_LR", "SB Vag1_3", "SB Vag1_9" },
		{ "vag2_FB", "SB Vag2_0", "SB Vag2_6" }, { "vag2_LR", "SB Vag2_3", "SB Vag2_9" },
		{ "vag3_FB", "SB Vag3_0", "SB Vag3_6" }, { "vag3_LR", "SB Vag3_3", "SB Vag3_9" },
		{ "vag4_FB", "SB Vag4_0", "SB Vag4_6" }, { "vag4_LR", "SB Vag4_3", "SB Vag4_9" },
		{ "vag5_FB", "SB Vag5_0", "SB Vag5_6" }, { "vag5_LR", "SB Vag5_3", "SB Vag5_9" },
	};

	// A-Body's penis chain: tip, middle, base (the _SMP bones are the simulated ones)
	// the mesh builder's rim profile (tools/genitals.py RIM_PENIS, VAG_RIM_REACH, ANAL_RIM_REACH), at scale 1
	constexpr float kRimPenis = 2.0F;
	constexpr float kReach[2][2] = { { 3.0F, 4.5F }, { 2.5F, 4.0F } };
	constexpr const char* kPenis[] = { "Penis_05_SMP", "Penis_03_SMP", "Penis_01_SMP" };
	// where the plugin bends him: the root of A-Body's penis chain, above the animated Penis_00

	// The openings, from her kinematic anchors (they hang still off her pelvis): the centre of the rim, and a point
	// down the canal for its axis
	struct Opening
	{
		const char*                name;
		std::array<const char*, 4> rim;
		std::array<const char*, 4> deep;
		float                      aimOut;  // the point aimed at: this far out from the rim's centre, along the canal
	};
	// each: its visible rim's ring (the vagina's at the back of the slit, where a real entrance is) and a canal ring
	constexpr Opening kVagina{ "vagina", { "SB VagRim_0 Base", "SB VagRim_6 Base", "SB VagRim_12 Base", "SB VagRim_18 Base" },
		{ "SB Vag2_0 Base", "SB Vag2_3 Base", "SB Vag2_6 Base", "SB Vag2_9 Base" }, 0.0F };
	constexpr Opening kAnus{ "anus", { "SB AnusRim_0 Base", "SB AnusRim_6 Base", "SB AnusRim_12 Base", "SB AnusRim_18 Base" },
		{ "SB AnusT2 Base", "SB AnusB2 Base", "SB AnusL2 Base", "SB AnusR2 Base" }, 0.8F };
	// The anus's rim sits at the bottom of GT's pucker, 0.8 inside her skin along the canal. Aimed at the rim's centre,
	// a penis coming in at an angle crossed her skin beside the hole, on the funnel's side (v0.25): the anus is aimed
	// at where the canal's line meets her skin instead.

	// her body's physics files, as the NIF names them (relative to Data)
	constexpr const char* kXmlBoth = "meshes\\SoftBodyFO4\\SoftBody-CBBE.xml";
	constexpr const char* kXmlVaginal = "meshes\\SoftBodyFO4\\SoftBody-CBBE-Vaginal.xml";
	constexpr const char* kXmlAnal = "meshes\\SoftBodyFO4\\SoftBody-CBBE-Anal.xml";

	constexpr auto kInterval = 25ms;
	constexpr auto kSettle = 1500ms;  // after a scene change or a move, before measuring
	constexpr auto kMeasure = 2500ms;  // a measurement: about one loop of a thrusting animation

	FourStimAPI::ThreadInterface*    threads{ nullptr };
	FourStimAPI::AlignmentInterface* alignment{ nullptr };
	FourStimAPI::SceneInterface*     scenes{ nullptr };

	std::atomic<std::int32_t> followed{ -1 };
	std::atomic_bool          pending{ false };
	Clock::time_point         since;
	std::atomic<std::int32_t> samples{ 0 };
	std::atomic<std::int32_t> herPosition{ -1 };
	std::atomic<std::int32_t> himPosition{ -1 };
	std::atomic<float>        hisHeading{ 0.0F };

	RE::NiAVObject* Find(RE::NiAVObject* a_root, const char* a_name)
	{
		return a_root ? a_root->GetObjectByName(RE::BSFixedString(a_name)) : nullptr;
	}

	RE::Actor* ActorAt(FourStimAPI::Thread* a_thread, std::int32_t a_position)
	{
		auto* actor = a_thread && a_position >= 0 ? a_thread->getActor(static_cast<std::uint32_t>(a_position)) : nullptr;
		return actor ? static_cast<RE::Actor*>(actor->getGameActor()) : nullptr;
	}

	void Notify(std::string a_text)
	{
		REX::INFO("{}", a_text);
		if (auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask([text = std::move(a_text)]() { RE::SendHUDMessage::ShowHUDMessage(("SoftBody: " + text).c_str(), nullptr, true, false); });
		}
	}

	void AppendPoint(std::string& a_line, const V& a_point)
	{
		a_line += std::format(",{:.3f},{:.3f},{:.3f}", a_point.x, a_point.y, a_point.z);
	}

	std::string Header()
	{
		std::string header = "header,n,t,scene";
		for (const auto& w : kWidths) {
			header += std::format(",{}", w.name);
		}
		for (const auto& p : kBones) {
			header += std::format(",{0}.x,{0}.y,{0}.z", p.bone);
		}
		for (const auto* name : kPenis) {
			header += std::format(",{0}.x,{0}.y,{0}.z", name);
		}
		return header + ",cervixPush,ballsTurn,ballsMove,ballsKept,ballLTurn,ballLMove,ballLKept,ballRTurn,ballRMove,ballRKept";
	}

	// The nodes read, held (NiPointer) from scene start to scene end so a read off the main thread can't meet a freed
	// node. Keys: "her:<node>" and "him:<node>".
	std::mutex                                                      heldLock;
	std::unordered_map<std::string, RE::NiPointer<RE::NiAVObject>> held;
	std::string                                                     scene;
	std::atomic_bool                                                resolved{ false };  // her nodes held
	std::atomic_bool                                                complete{ false };  // his too
	int                                                             attempts{ 0 };

	RE::NiAVObject* Held(const char* a_who, const char* a_name)
	{
		const auto it = held.find(std::format("{}:{}", a_who, a_name));
		return it != held.end() ? it->second.get() : nullptr;
	}

	// her: the actor with SoftBody's genital bones; him: the one with A-Body's penis
	std::atomic<std::uint32_t> herFormID{ 0 };  // the woman followed (for SoftBodyFO4API.h)

	void Identify(FourStimAPI::Thread* a_thread, RE::NiAVObject** a_her, RE::NiAVObject** a_him)
	{
		std::int32_t herAt = -1, himAt = -1;
		for (std::uint32_t i = 0; i < a_thread->getActorCount(); ++i) {
			auto* actor = ActorAt(a_thread, static_cast<std::int32_t>(i));
			auto* root = actor ? actor->Get3D() : nullptr;
			if (herAt < 0 && Find(root, "VaginaDB02")) {
				herAt = static_cast<std::int32_t>(i);
				if (a_her) *a_her = root;
				herFormID = actor->GetFormID();
			} else if (himAt < 0 && Find(root, kPenis[0])) {
				himAt = static_cast<std::int32_t>(i);
				if (a_him) *a_him = root;
				hisHeading = actor->data.angle.z;
			}
		}
		herPosition = herAt;
		himPosition = himAt;
	}

	enum class Act
	{
		kNone,
		kVaginal,
		kAnal,
		kBoth
	};

	// the act going on (from 4Stim's scene, the penis, or F11)
	std::atomic<Act> currentAct{ Act::kNone };

	// ---------------------------------------------------------------- the openings' rims, round the penis
	// Left to collision, the rims opened where the thrusting penis shoved their pads: along the slit, lagging, oval.
	// So her physics files make the rims' bones kinematic, and here each is pushed straight out from the penis's axis
	// to its radius, from where it rests, wherever the penis (shaft and rounded tip) passes within that radius of it.
	// The skin on them opens round the shaft and closes behind it. Nothing else of hers is moved.
	struct Rim
	{
		std::string name;
		int         opening;  // 0 the vagina, 1 the anus: its centre gives the way out, and the edge's place
		bool        outer{ false };  // an outer ring's: kept outside the inner, where the skin between keeps its area
		bool        field{ false };  // not a ring's: carried by the opening in use as the skin round it is (below)
		int         canal{ -1 };     // the vaginal canal's rings (0 the entrance, then 1-5 deeper): each on its own centre
		int         womb{ -1 };      // the cervix and uterus (v0.28): 0 the os ring, 1 the cavity's ring, 2 its top
	};
	// GT's lip and anus bones are no longer driven (v0.21): moved on their own they folded the skin round the rims.
	// Each visible hole has an inner ring of 24 (12 showed as a polygon) and an outer ring of 12 (a shape's bones are
	// capped: see the mesh builder's MAX_SHAPE_BONES), and the vagina its entrance ring of 12.
	constexpr int kCanalRings = 5;  // SB Vag1_* .. SB Vag5_*, behind the entrance ring SB VagIn_*
	constexpr int kVagRings = kCanalRings + 1;
	constexpr int kAnalRings = 3;   // SB Anus{T,L,B,R}1-3, the anal canal's (v0.29: driven as the vagina's are)
	constexpr float kCervixBeyond = 0.8F;  // the canal's closed end past its last ring (tools/genitals.py VAG_DEPTH - VAG_RINGS[-1])
	std::atomic<float> cervixPush{ 0 };      // how far his tip carries it now (logged)
	// requests from other plugins (SoftBodyFO4API.h) and what is published back
	std::atomic<float>         uterusFill{ 0 };
	std::atomic<float>         osOpen{ 0 };
	std::mutex                 publishedLock;
	SoftBodyFO4API::Canal      published{};
	const std::vector<Rim>& Rims()
	{
		static const std::vector<Rim> rims = [] {
			std::vector<Rim> r;
			for (int k = 0; k < 12; ++k) r.push_back({ std::format("SB VagIn_{}", k), 0, false, false, 0 });
			for (int i = 1; i <= kCanalRings; ++i) {
				for (int k = 0; k < 12; ++k) r.push_back({ std::format("SB Vag{}_{}", i, k), 0, false, false, i });
			}
			for (int i = 1; i <= kAnalRings; ++i) {
				for (const char* side : { "T", "L", "B", "R" }) {
					r.push_back({ std::format("SB Anus{}{}", side, i), 1, false, false, kVagRings + i - 1 });
				}
			}
			for (int k = 0; k < 8; ++k) r.push_back({ std::format("SB Os_{}", k), 0, false, false, -1, 0 });
			for (int k = 0; k < 8; ++k) r.push_back({ std::format("SB Uterus_{}", k), 0, false, false, -1, 1 });
			r.push_back({ "SB UterusTop", 0, false, false, -1, 2 });
			for (int k = 0; k < 24; ++k) {
				r.push_back({ std::format("SB VagRim_{}", k), 0 });
				r.push_back({ std::format("SB AnusRim_{}", k), 1 });
			}
			for (int k = 0; k < 12; ++k) {
				r.push_back({ std::format("SB VagRimO_{}", k), 0, true });
				r.push_back({ std::format("SB AnusRimO_{}", k), 1, true });
			}
			// GT's lip and anus bones: the skin on them (677 of the hood's points on Clitoral1) stayed put while the
			// rings round it opened, and folded (v0.23)
			for (const char* n : { "Clitoral1", "VaginaB1", "NPC L Pussy02", "NPC R Pussy02" }) r.push_back({ n, 0, false, true });
			for (const char* n : { "NPC LT Anus2", "NPC RT Anus2", "NPC LB Anus2", "NPC RB Anus2" }) r.push_back({ n, 1, false, true });
			return r;
		}();
		return rims;
	}

	struct Driven
	{
		RE::NiPointer<RE::NiAVObject> node;
		V                             rest;  // its own local position, as the NIF has it
		V                             now;
		int                           opening{ 0 };
		bool                          outer{ false };
		bool                          edge{ false };  // a visible hole's inner ring: its rest radius is the hole's
		bool                          field{ false };
		int                           canal{ -1 };
		int                           womb{ -1 };
	};
	std::unordered_map<std::string, Driven> driven;  // under heldLock

	// on the main thread, from Resolve: a rim bone's rest is taken once, when it is first seen
	void HoldRim(RE::NiAVObject* a_her, const Rim& a_rim)
	{
		auto* node = Find(a_her, a_rim.name.c_str());
		if (!node) {
			return;
		}
		auto& d = driven[a_rim.name];
		if (d.node.get() != node) {
			d = Driven{ RE::NiPointer<RE::NiAVObject>(node), node->local.translate, node->local.translate, a_rim.opening,
				a_rim.outer, !a_rim.outer && a_rim.name.find("Rim_") != std::string::npos, a_rim.field, a_rim.canal, a_rim.womb };
			if (a_rim.name.ends_with("_0")) {
				REX::INFO("rim {}: rest ({:.3f}, {:.3f}, {:.3f})", a_rim.name, d.rest.x, d.rest.y, d.rest.z);
			}
		}
	}

	// ---------------------------------------------------------------- his penis, turned along its shaft through the opening
	// The aligner places him once a scene; through a stroke the animation swings his penis 10-20 degrees, so his line
	// passes up to a unit off the opening's centre. Here, every few milliseconds, his penis is turned so his line runs
	// through the centre of the opening in use, by at most fBendMax. v0.22 turned him at his root (Penis_00_Offset): the
	// skin where his shaft meets his body, weighted between the turned bones and Penis_Offset_SMP, tore into sheets (at
	// 15 degrees, A-Body's mesh had 222 edges stretched over 3x). So the turn is shared by the shaft's first two joints,
	// Penis_02_Offset (at Penis_01_SMP, his line's base) and Penis_03_Offset, which A-Body's weights blend smoothly
	// across (1.5x at most at 20 degrees). v0.23 wrote those joints, but something rewrites them every frame (the log
	// counted 16000 rewrites in a scene), so the bend rarely showed. His skin hangs on the _SMP leaves under them, which
	// nothing animates: those are turned instead, about the joints, as if the joints had turned. FO4 keeps a node's
	// rotation as the map from its parent's frame to its own (see ToLocal), so turning a node by Q (in its parent's
	// frame) takes local.rotate * Q^T.
	struct BendPivot
	{
		const char* name;
		float       share;
	};
	constexpr BendPivot kBendPivots[2] = { { "Penis_02_Offset", 0.5F }, { "Penis_03_Offset", 0.5F } };
	constexpr float kBendSpeed = 200.0F;     // units per second a bent skin bone may move, relative to his base
	std::uint32_t   bendClamped = 0;
	struct BendLeaf
	{
		const char*                   name;
		int                           pivots;   // how many of kBendPivots (from the first) it hangs below
		RE::NiPointer<RE::NiAVObject> node;
		RE::NiTransform               base;     // its own place, as the NIF has it
		RE::NiTransform               written;  // what was last written
		std::uint32_t                 rebased{ 0 };
		V                             rel;              // where it was last placed, from his base (v0.38)
		bool                          placed{ false };
	};
	struct Bend
	{
		BendLeaf leaves[4]{ { "Penis_02_SMP", 1 }, { "Penis_03_SMP", 2 }, { "Penis_04_SMP", 2 }, { "Penis_05_SMP", 2 } };
		V              turn;
		bool           started{ false };
		float          weight{ 0 };  // how far the shaft is laid along the path (eased)
		std::vector<V> path;         // the last path he was laid along (kept while easing out)
	} bend;

	RE::NiMatrix3 AxisAngle(const V& a_turn)
	{
		RE::NiMatrix3 m;
		const float   angle = Len(a_turn);
		const V       k = angle > 1e-7F ? Mul(a_turn, 1.0F / angle) : V(1, 0, 0);
		const float   c = std::cos(angle), s = std::sin(angle), t = 1 - c;
		const float   a[3] = { k.x, k.y, k.z };
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				m.entry[i][j] = t * a[i] * a[j] + (i == j ? c : 0.0F);
			}
		}
		m.entry[0][1] -= s * k.z; m.entry[0][2] += s * k.y;
		m.entry[1][0] += s * k.z; m.entry[1][2] -= s * k.x;
		m.entry[2][0] -= s * k.y; m.entry[2][1] += s * k.x;
		return m;
	}

	bool Same(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
	{
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				if (std::abs(a.entry[i][j] - b.entry[i][j]) > 1e-5F) return false;
		return true;
	}

	// a times b transposed
	RE::NiMatrix3 TimesTransposed(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
	{
		RE::NiMatrix3 m;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				m.entry[i][j] = a.entry[i][0] * b.entry[j][0] + a.entry[i][1] * b.entry[j][1] + a.entry[i][2] * b.entry[j][2];
		return m;
	}

	RE::NiMatrix3 Transposed(const RE::NiMatrix3& a)
	{
		RE::NiMatrix3 m;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				m.entry[i][j] = a.entry[j][i];
		return m;
	}

	// the turn (axis times angle) a rotation matrix makes: AxisAngle's inverse
	V TurnOf(const RE::NiMatrix3& m)
	{
		const float c = std::clamp((m.entry[0][0] + m.entry[1][1] + m.entry[2][2] - 1.0F) * 0.5F, -1.0F, 1.0F);
		const float angle = std::acos(c);
		const V     k(m.entry[2][1] - m.entry[1][2], m.entry[0][2] - m.entry[2][0], m.entry[1][0] - m.entry[0][1]);
		const float s = Len(k);
		return angle < 1e-5F || s < 1e-7F ? V(0, 0, 0) : Mul(k, angle / s);
	}

	// a world direction in a node's own frame (no scale)
	V DirIn(const RE::NiAVObject* a_node, const V& a_world)
	{
		const auto& e = a_node->world.rotate.entry;
		return V(e[0][0] * a_world.x + e[0][1] * a_world.y + e[0][2] * a_world.z,
			e[1][0] * a_world.x + e[1][1] * a_world.y + e[1][2] * a_world.z,
			e[2][0] * a_world.x + e[2][1] * a_world.y + e[2][2] * a_world.z);
	}

	// a direction in a node's own frame, in the world (DirIn's inverse)
	V DirOut(const RE::NiAVObject* a_node, const V& a_local)
	{
		const auto& e = a_node->world.rotate.entry;
		return V(e[0][0] * a_local.x + e[1][0] * a_local.y + e[2][0] * a_local.z,
			e[0][1] * a_local.x + e[1][1] * a_local.y + e[2][1] * a_local.z,
			e[0][2] * a_local.x + e[1][2] * a_local.y + e[2][2] * a_local.z);
	}

	// v rotated by angle about the (unit) axis k
	V Rotate(const V& a_v, const V& a_k, float a_angle)
	{
		const float c = std::cos(a_angle), s = std::sin(a_angle);
		return Add(Add(Mul(a_v, c), Mul(Cross(a_k, a_v), s)), Mul(a_k, Dot(a_k, a_v) * (1 - c)));
	}

	bool SameTransform(const RE::NiTransform& a, const RE::NiTransform& b)
	{
		return Same(a.rotate, b.rotate) && Len(Sub(a.translate, b.translate)) < 1e-5F;
	}

	void ReleaseBend()
	{
		for (auto& l : bend.leaves) {
			if (auto* node = l.node.get(); node && SameTransform(node->local, l.written)) {
				node->local.rotate = l.base.rotate;
				node->local.translate = l.base.translate;
			}
			l.node.reset();
			l.rebased = 0;
			l.placed = false;
		}
		bend.turn = V(0, 0, 0);
		bend.started = false;
		bend.weight = 0;
		bend.path.clear();
	}

	// v0.40/v0.41: his balls calmed. A-Body's ball springs (stiffness and damping 999, bounce 0.4) fling and stretch
	// them. SMP sets their world transforms and leaves their local ones at the NIF's (v0.40 watched the local ones and
	// saw nothing), so each 4 ms each ball bone's place in its parent, from its world one, is held within fBallMax
	// (turn) and fBallStretch (move) of its local (rest) one, eased in over fBallSmoothing, and written back to its world
	// transform. Parents first, so a child follows its parent as drawn. SMP keeps simulating; only what is drawn changes
	struct Ball
	{
		const char*                   name;
		RE::NiPointer<RE::NiAVObject> node;
		RE::NiTransform               written;          // the world transform last written
		RE::NiMatrix3                 hangTurn;         // where it hangs in its parent (v0.42: from SMP itself, drifting;
		V                             hangAt;           // the NIF's local rest was 150 degrees and 4 units off SMP's)
		V                             simTurn, simMove;   // SMP's, from the hang
		V                             turn, move;         // drawn, from the hang
		bool                          started{ false }, hung{ false };
		std::uint32_t                 wrote{ 0 }, kept{ 0 };   // writes, and how many were still there next time (logged)
	};
	Ball balls[3]{ { "Penis_Balls_01_SMP" }, { "Penis_Ball_SMP.L" }, { "Penis_Ball_SMP.R" } };

	void ReleaseBalls()
	{
		for (auto& b : balls) {
			b.node.reset();
			b.started = b.hung = false;
			b.simTurn = b.simMove = b.turn = b.move = V(0, 0, 0);
			b.wrote = b.kept = 0;
		}
	}

	RE::NiMatrix3 Times(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
	{
		return TimesTransposed(a, Transposed(b));
	}

	V Apply(const RE::NiMatrix3& m, const V& v)
	{
		return V(m.entry[0][0] * v.x + m.entry[0][1] * v.y + m.entry[0][2] * v.z,
			m.entry[1][0] * v.x + m.entry[1][1] * v.y + m.entry[1][2] * v.z,
			m.entry[2][0] * v.x + m.entry[2][1] * v.y + m.entry[2][2] * v.z);
	}

	V Clamped(const V& v, float a_most)
	{
		const float l = Len(v);
		return l > a_most ? Mul(v, a_most / l) : v;
	}

	// under heldLock, from DriveRims
	void CalmBalls(float a_dt)
	{
		const float most = Settings::ballMax * 0.0174533F;
		const float follow = Settings::ballSmoothing > 0 ? 1.0F - std::exp(-a_dt / Settings::ballSmoothing) : 1.0F;
		const float drift = 1.0F - std::exp(-a_dt / 1.0F);      // the hang follows what is drawn over about a second
		RE::NiAVObject* nodes[3]{};
		for (int i = 0; i < 3; ++i) {               // SMP's places, before any is rewritten
			auto& b = balls[i];
			auto* node = Settings::calmBalls ? Held("him", b.name) : nullptr;
			if (!node || !node->parent) {
				b.started = false;
				continue;
			}
			if (!b.started || b.node.get() != node) {
				b.node = RE::NiPointer<RE::NiAVObject>(node);
				b.simTurn = b.simMove = b.turn = b.move = V(0, 0, 0);
				b.written = RE::NiTransform{};
				b.started = true;
				b.hung = false;
			}
			nodes[i] = node;
			if (!SameTransform(node->world, b.written)) {      // SMP (or the game) set it since
				const auto& pw = node->parent->world;
				const auto  rel = TimesTransposed(Transposed(pw.rotate), Transposed(node->world.rotate));  // parent^T * world
				const V     at = Mul(Apply(Transposed(pw.rotate), Sub(node->world.translate, pw.translate)),
                    pw.scale > 1e-6F ? 1.0F / pw.scale : 1.0F);
				if (!b.hung) {
					b.hangTurn = rel;
					b.hangAt = at;
					b.hung = true;
				}
				b.simTurn = TurnOf(TimesTransposed(rel, b.hangTurn));
				b.simMove = Sub(at, b.hangAt);
			} else if (b.wrote) {
				++b.kept;
			}
		}
		for (int i = 0; i < 3; ++i) {               // drawn: held, eased, and composed on the parent as drawn
			auto& b = balls[i];
			auto* node = nodes[i];
			if (!node) {
				continue;
			}
			b.turn = Add(b.turn, Mul(Sub(Clamped(b.simTurn, most), b.turn), follow));
			b.move = Add(b.move, Mul(Sub(Clamped(b.simMove, Settings::ballStretch), b.move), follow));
			// the hang drifts after what is drawn, so a lasting sag stays and a fling doesn't
			const V turnSettle = Mul(b.turn, drift), moveSettle = Mul(b.move, drift);
			b.hangTurn = Times(AxisAngle(turnSettle), b.hangTurn);
			b.hangAt = Add(b.hangAt, moveSettle);
			b.turn = Sub(b.turn, turnSettle);
			b.move = Sub(b.move, moveSettle);
			b.simTurn = Sub(b.simTurn, turnSettle);
			b.simMove = Sub(b.simMove, moveSettle);
			const auto&     pw = node->parent->world;
			RE::NiTransform w = node->world;
			w.rotate = Times(pw.rotate, Times(AxisAngle(b.turn), b.hangTurn));
			w.translate = Add(pw.translate, Mul(Apply(pw.rotate, Add(b.hangAt, b.move)), pw.scale));
			node->world = w;
			b.written = w;
			++b.wrote;
		}
	}

	// v0.35: the snake path made a smooth curve. As a polyline it turned all at once at the entrance and at each canal
	// ring, and his skin bones, laid along it, kinked his shaft there. It leaves his base along his own line (a_u) for a
	// third of the way to the entrance, so the bend starts gently from his root, then Chaikin's corner cutting (four
	// rounds, ends kept) rounds every turn
	std::vector<V> SmoothPath(const std::vector<V>& a_path, const V& a_u)
	{
		if (a_path.size() < 2) {
			return a_path;
		}
		std::vector<V> p{ a_path[0], Add(a_path[0], Mul(a_u, Len(Sub(a_path[1], a_path[0])) / 3.0F)) };
		p.insert(p.end(), a_path.begin() + 1, a_path.end());
		for (int round = 0; round < 4; ++round) {
			std::vector<V> q{ p.front() };
			for (std::size_t k = 0; k + 1 < p.size(); ++k) {
				q.push_back(Add(Mul(p[k], 0.75F), Mul(p[k + 1], 0.25F)));
				q.push_back(Add(Mul(p[k], 0.25F), Mul(p[k + 1], 0.75F)));
			}
			q.push_back(p.back());
			p.swap(q);
		}
		return p;
	}

	// A point `a_s` along a polyline from its first point (past its end, carried on along its last piece), and the
	// polyline's direction there
	V AlongPath(const std::vector<V>& a_path, float a_s, V* a_dir)
	{
		float walked = 0;
		for (std::size_t k = 0; k + 1 < a_path.size(); ++k) {
			const V     seg = Sub(a_path[k + 1], a_path[k]);
			const float len = Len(seg);
			if (len < 1e-4F) {
				continue;
			}
			const bool last = k + 2 == a_path.size();
			if (a_s <= walked + len || last) {
				*a_dir = Mul(seg, 1.0F / len);
				return Add(a_path[k], Mul(*a_dir, a_s - walked));
			}
			walked += len;
		}
		*a_dir = V(0, 0, 1);
		return a_path.empty() ? V(0, 0, 0) : a_path.back();
	}

	// The snake aim (v0.31; the idea is fo4-ocbpc's, by ReidenXerx, written afresh here). His shaft runs straight from
	// its base (Penis_01_SMP) to the entrance of the opening in use, then along her canal's middle (its rings' rest
	// centres, to the cervix), each skin bone (Penis_02_SMP..05_SMP) placed at its own distance along that path and
	// turned to it. Before his tip reaches the entrance the same path just points him at it. v0.23-v0.30 only turned
	// him toward the opening's centre and had the canal follow his line: 22-49 degrees off it, the canal swung up to 4
	// units and tore. Eased in and out over fBendSmoothing; given up (eased out) where the entrance is more than
	// fBendMax off his line, or the canal turns more than fSnakeMax from it.
	void BendPenis(float a_dt, bool a_penis, const V& a_base, const V& a_u, float a_length, const std::vector<V>* a_paths,
		Act a_act)
	{
		RE::NiAVObject* leaves[4]{};
		bool            all = Settings::bendPenis;
		for (int i = 0; all && i < 4; ++i) {
			leaves[i] = Held("him", bend.leaves[i].name);
			all = leaves[i] && leaves[i]->parent;
		}
		if (!all) {
			if (bend.started) {
				ReleaseBend();
			}
			return;
		}
		bool same = bend.started;
		for (int i = 0; same && i < 4; ++i) {
			same = bend.leaves[i].node.get() == leaves[i];
		}
		if (!same) {
			ReleaseBend();
			for (int i = 0; i < 4; ++i) {
				auto& l = bend.leaves[i];
				l.node = RE::NiPointer<RE::NiAVObject>(leaves[i]);
				l.base = l.written = leaves[i]->local;
			}
			bend.started = true;
			REX::INFO("bend: laying his shaft's skin bones along the opening in use");
		} else {
			for (auto& l : bend.leaves) {
				if (!SameTransform(l.node->local, l.written)) {
					l.base = l.node->local;  // something else set it since: turn from there
					if (++l.rebased % 500 == 1) {
						REX::INFO("bend: {} was set by something else ({} times)", l.name, l.rebased);
					}
				}
			}
		}
		// his line as the animation has it (the tip bone's own place, not where this laid it), so the bend never
		// measures itself
		const V tip0 = ToWorld(leaves[3]->parent, bend.leaves[3].base.translate);
		const V u0 = Len(Sub(tip0, a_base)) > 1e-3F ? Unit(Sub(tip0, a_base)) : a_u;
		// the path: his base, the entrance, then inside her
		const int o = a_act == Act::kVaginal ? 0 : a_act == Act::kAnal ? 1 : -1;
		std::vector<V> path;
		bool           locked = false;
		if (a_penis && o >= 0 && a_paths[o].size() >= 2) {
			const V&    entrance = a_paths[o][0];
			const V     toEntrance = Sub(entrance, a_base);
			const float reach = Len(toEntrance);
			const float most = Settings::bendMax * 0.0174533F;
			const float snakeMost = Settings::snakeMax * 0.0174533F;
			const V     inward = Unit(Sub(a_paths[o].back(), entrance));
			const float off = reach > 1e-3F ? std::acos(std::clamp(Dot(Unit(toEntrance), u0), -1.0F, 1.0F)) : 0.0F;
			const float turn = std::acos(std::clamp(Dot(inward, u0), -1.0F, 1.0F));
			if (reach > 1.0F && reach < a_length + 6.0F && off <= most && turn <= snakeMost) {
				path.push_back(a_base);
				for (const auto& q : a_paths[o]) {
					path.push_back(q);
				}
				locked = true;
			}
		}
		const float follow = Settings::bendSmoothing > 0 ? 1.0F - std::exp(-a_dt / Settings::bendSmoothing) : 1.0F;
		bend.weight += ((locked ? 1.0F : 0.0F) - bend.weight) * follow;
		if (locked) {
			bend.path = SmoothPath(path, a_u);
		}
		const float w = bend.weight;
		if (w < 1e-3F || bend.path.size() < 2) {
			for (int i = 0; i < 4; ++i) {
				auto& l = bend.leaves[i];
				l.written = l.base;
				leaves[i]->local.rotate = l.base.rotate;
				leaves[i]->local.translate = l.base.translate;
			}
			return;
		}
		for (int i = 0; i < 4; ++i) {
			auto&   l = bend.leaves[i];
			auto*   parent = leaves[i]->parent;
			const V now = ToWorld(parent, l.base.translate);       // where the animation has it
			const float s = Dot(Sub(now, a_base), u0);           // how far along his shaft
			V       dir;
			const V there = AlongPath(bend.path, std::max(0.0F, s), &dir);
			V placed = Add(now, Mul(Sub(there, now), w));
			// v0.38: a skin bone moves at most kBendSpeed (per second) from where it was last placed, measured from
			// his base, so moving him (the aligner) moves it freely. A bad frame of his line or her canal (one 25 ms
			// sample in doggy folded his shaft to 150 degrees and stretched it from 11 to 18) now can't throw it
			V rel = Sub(placed, a_base);
			if (l.placed) {
				const V     step = Sub(rel, l.rel);
				const float most = std::max(0.2F, kBendSpeed * a_dt);
				if (const float len = Len(step); len > most) {
					rel = Add(l.rel, Mul(step, most / len));
					if (++bendClamped % 200 == 1) {
						REX::INFO("bend: held back a jump of {:.2f} in {:.0f} ms ({} times)", len, a_dt * 1000.0F, bendClamped);
					}
				}
			}
			l.rel = rel;
			l.placed = true;
			placed = Add(a_base, rel);
			const V axis = Cross(u0, dir);
			const float sn = std::min(1.0F, Len(axis));
			const float angle = std::asin(sn) * (Dot(u0, dir) < 0 ? -1.0F : 1.0F) * w;
			l.written = l.base;
			if (sn > 1e-5F) {
				l.written.rotate = TimesTransposed(l.base.rotate, AxisAngle(Mul(DirIn(parent, Unit(axis)), angle)));
			}
			l.written.translate = ToLocal(parent, placed);
			leaves[i]->local.rotate = l.written.rotate;
			leaves[i]->local.translate = l.written.translate;
		}
	}

	// off the main thread, every few milliseconds, under heldLock
	void DriveRims(float a_dt)
	{
		auto*      tip = Held("him", kPenis[0]);
		auto*      mid = Held("him", kPenis[1]);
		auto*      base = Held("him", kPenis[2]);
		const bool penis = Settings::driveOpenings && tip && base;
		V          b(0, 0, 0), u(0, 0, 0), m(0, 0, 0);
		float      length = 0, scale = 1, first = 0;
		if (penis) {
			b = base->world.translate;
			const V t = tip->world.translate;
			u = Unit(Sub(t, b));
			scale = tip->world.scale;
			m = mid ? mid->world.translate : Add(b, Mul(Sub(t, b), 0.5F));
			first = Len(Sub(m, b));
			length = first + Len(Sub(t, m)) + Settings::penisTipExtra * scale;
		}
		// the bend curves his shaft (two joints), so each opening goes by the piece of it that passes it: base to
		// middle bone, or middle to tip. segStart/segU: that piece; segAlong: how far along him it starts
		V     segStart[2], segU[2];
		float segAlong[2]{};
		// each opening's centre, from its kinematic rim anchors
		V    centre[2], aim[2];
		bool haveCentre[2]{};
		for (int o = 0; o < 2; ++o) {
			const Opening& op = o == 0 ? kVagina : kAnus;
			V              c(0, 0, 0);
			int            n = 0;
			for (const auto* name : op.rim) {
				if (auto* r = Held("her", name)) {
					c = Add(c, r->world.translate);
					++n;
				}
			}
			haveCentre[o] = n == 4;
			centre[o] = Mul(c, 0.25F);
			aim[o] = centre[o];
			if (op.aimOut > 0) {
				V   dc(0, 0, 0);
				int dn = 0;
				for (const auto* name : op.deep) {
					if (auto* r = Held("her", name)) {
						dc = Add(dc, r->world.translate);
						++dn;
					}
				}
				if (dn == 4) {
					const V axis = Unit(Sub(Mul(dc, 0.25F), centre[o]));
					aim[o] = Sub(centre[o], Mul(axis, op.aimOut * scale));
					if (o == 1 && haveCentre[0] && Settings::anusAimBack != 0) {
						V back = Sub(centre[1], centre[0]);   // the vagina's centre is found first
						back = Sub(back, Mul(axis, Dot(back, axis)));
						aim[o] = Add(aim[o], Mul(Unit(back), Settings::anusAimBack * scale));
					}
				}
			}
			segStart[o] = b;
			segU[o] = u;
			if (penis && first > 1e-3F && Len(Sub(tip->world.translate, m)) > 1e-3F) {
				const V u1 = Mul(Sub(m, b), 1.0F / first);
				if (Dot(Sub(centre[o], b), u1) <= first) {
					segU[o] = u1;
				} else {
					segStart[o] = m;
					segU[o] = Unit(Sub(tip->world.translate, m));
					segAlong[o] = first;
				}
			}
		}
		// only the opening in use opens: the perineum is short, so a penis in one passes within reach of the other's
		// rim (v0.19: an anal penis opened the vagina's under it)
		const Act act = currentAct.load();
		const bool open[2] = { act != Act::kAnal, act != Act::kVaginal };
		const float follow = Settings::rimSmoothing > 0 ? 1.0F - std::exp(-a_dt / Settings::rimSmoothing) : 1.0F;
		const float tipLength = Settings::penisTipLength * scale;
		const float shaft = length - Settings::penisTipExtra * scale;
		// his thickness at a point `along` his line from the base bone: thinning from the root to the head (A-Body's
		// measures 2.2 at the root, 1.8 two-thirds along), rounded off over the tip
		auto thickness = [&](float a_along) {
			const float f = shaft > 0 ? std::clamp(a_along / shaft, 0.0F, 1.0F) : 0.0F;
			const float r = (Settings::penisRadius + (Settings::penisRadiusTip - Settings::penisRadius) * f) * scale;
			const float fromTip = length - a_along;
			return fromTip >= tipLength ? r : r * std::sqrt(std::max(0.0F, fromTip) / tipLength);
		};
		// Each opening as a whole: where the penis passes its centre, how thick he is there, and how far off the
		// centre. Its rings go round him: their centre moves onto his line (up to fRimFollow) and they open to his
		// thickness. v0.20 pushed each bone out from his line on its own, so a line a unit off the centre (as he
		// swings through a stroke) pushed both sides of the ring the same way and it shut.
		bool  active[2]{};
		V     shift[2];
		float grow[2]{};
		float edgeRest[2]{};  // the hole's radius at rest (its inner ring's)
		int   edgeCount[2]{};
		float outerRest[2]{};  // its outer ring's
		int   outerCount[2]{};
		for (auto& [name, d] : driven) {
			if ((d.edge || d.outer) && d.node && d.node->parent && haveCentre[d.opening]) {
				const V&    su = segU[d.opening];
				const V     off = Sub(ToWorld(d.node->parent, d.rest), centre[d.opening]);
				const float r = Len(Sub(off, Mul(su, Dot(off, su))));
				if (d.edge) {
					edgeRest[d.opening] += r;
					++edgeCount[d.opening];
				} else {
					outerRest[d.opening] += r;
					++outerCount[d.opening];
				}
			}
		}
		for (int o = 0; o < 2; ++o) {
			edgeRest[o] = edgeCount[o] ? edgeRest[o] / edgeCount[o] : Settings::rimEdge * scale;
			outerRest[o] = outerCount[o] ? outerRest[o] / outerCount[o] : 3.0F * edgeRest[o];
			if (!penis || !open[o] || !haveCentre[o]) {
				continue;
			}
			const V     rel = Sub(centre[o], segStart[o]);
			const float onSeg = Dot(rel, segU[o]);
			const float along = segAlong[o] + onSeg;
			if (along <= -Settings::penisBaseExtra * scale || along >= length) {
				continue;
			}
			const float r = thickness(along) + Settings::rimMargin * scale;
			const V     off = Sub(Mul(segU[o], onSeg), rel);    // the centre to his line, across it
			const float dist = Len(off);
			if (dist > r + Settings::rimCatch * scale) {
				continue;
			}
			const float moved = std::min(dist, Settings::rimFollow * scale);
			active[o] = true;
			shift[o] = dist > 0 ? Mul(off, moved / dist) : V(0, 0, 0);
			grow[o] = r + (dist - moved);                       // what the hole must clear from its moved centre
		}
		// The vaginal canal's rings, each as an opening of its own: its centre (its bones' rest places) moves right onto
		// his line where he passes it (up to fCanalFollow), and it opens to his thickness there (rounding off over his
		// tip). Before v0.25 they were springs on fixed bases that his collision pushed: they stayed on the canal's own
		// line while he ran through it at 10-20 degrees.
		constexpr int kRings = kVagRings + kAnalRings;
		auto ringOpening = [](int a_i) { return a_i < kVagRings ? 0 : 1; };
		V     canalC[kRings], canalU[kRings], canalShift[kRings];
		float canalGrow[kRings]{};
		bool  canalOn[kRings]{};
		int   canalN[kRings]{};
		for (auto& [name, d] : driven) {
			if (d.canal >= 0 && d.node && d.node->parent) {
				canalC[d.canal] = Add(canalC[d.canal], ToWorld(d.node->parent, d.rest));
				++canalN[d.canal];
			}
		}
		for (int i = 0; i < kRings; ++i) {
			if (canalN[i]) {
				canalC[i] = Mul(canalC[i], 1.0F / canalN[i]);
			}
		}
		V     wombC[3];
		int   wombN[3]{};
		float wombR[3]{};
		for (auto& [name, d] : driven) {
			if (d.womb >= 0 && d.node && d.node->parent) {
				wombC[d.womb] = Add(wombC[d.womb], ToWorld(d.node->parent, d.rest));
				++wombN[d.womb];
			}
		}
		for (int i = 0; i < 3; ++i) {
			if (wombN[i]) {
				wombC[i] = Mul(wombC[i], 1.0F / wombN[i]);
			}
		}
		for (auto& [name, d] : driven) {
			if (d.womb >= 0 && d.womb < 2 && d.node && d.node->parent && wombN[d.womb]) {
				wombR[d.womb] += Len(Sub(ToWorld(d.node->parent, d.rest), wombC[d.womb])) / wombN[d.womb];
			}
		}
		const V     wombAxis = wombN[0] && wombN[2] ? Unit(Sub(wombC[2], wombC[0])) : V(0, 0, 1);
		const float wombLength = wombN[0] && wombN[2] ? Len(Sub(wombC[2], wombC[0])) : 0.0F;
		const float swell = std::clamp(uterusFill.load(), 0.0F, 1.0F) * Settings::uterusSwell;
		// The cervix: the canal's closed end, CERVIX_BEYOND past its last ring (which the cap rides on). His tip
		// pushes it rather than passing through it (v0.25): past it, the canal is carried deeper along his line by
		// as much (up to fCervixStretch), the push shared out along the canal by depth, so it stretches evenly from
		// the entrance (still) to the cervix (all of it)
		float push = 0;
		float ringDepth[kRings]{};
		if (penis && open[0] && Settings::driveCanal && canalN[0] && canalN[kVagRings - 1]) {
			const V     axis = Unit(Sub(canalC[kVagRings - 1], canalC[0]));
			const float span = Len(Sub(canalC[kVagRings - 1], canalC[0]));
			for (int i = 0; i < kVagRings; ++i) {
				ringDepth[i] = canalN[i] && span > 1e-3F ? std::clamp(Dot(Sub(canalC[i], canalC[0]), axis) / span, 0.0F, 1.0F) : 0.0F;
			}
			const V     end = Add(tip->world.translate, Mul(Unit(Sub(tip->world.translate, m)), Settings::penisTipExtra * scale));
			const float facing = Dot(u, axis);
			const float past = Dot(Sub(end, canalC[kVagRings - 1]), axis) - kCervixBeyond * scale;
			if (facing > 0.3F && past > 0) {
				push = std::min(past / facing, Settings::cervixStretch * scale);  // along his line
			}
		}
		cervixPush = push;
		for (int i = 0; i < kRings; ++i) {
			if (!canalN[i] || !penis || !open[ringOpening(i)] || !Settings::driveCanal) {
				continue;
			}
			V     start = b, su = u;
			float before = 0;
			if (first > 1e-3F && Len(Sub(tip->world.translate, m)) > 1e-3F) {
				const V u1 = Mul(Sub(m, b), 1.0F / first);
				if (Dot(Sub(canalC[i], b), u1) <= first) {
					su = u1;
				} else {
					start = m;
					su = Unit(Sub(tip->world.translate, m));
					before = first;
				}
			}
			const V     rel = Sub(canalC[i], start);
			const float onSeg = Dot(rel, su);
			const float along = before + onSeg;
			if (along <= -Settings::penisBaseExtra * scale || along >= length) {
				continue;
			}
			const float r = thickness(along) + Settings::rimMargin * scale;
			const V     off = Sub(Mul(su, onSeg), rel);
			const float dist = Len(off);
			if (dist > r + Settings::canalCatch * scale) {
				continue;
			}
			const float moved = std::min(dist, Settings::canalFollow * scale);
			canalOn[i] = true;
			canalU[i] = su;
			canalShift[i] = dist > 0 ? Mul(off, moved / dist) : V(0, 0, 0);
			canalGrow[i] = r + (dist - moved);
		}
		for (auto& [name, d] : driven) {
			auto* node = d.node.get();
			auto* parent = node ? node->parent : nullptr;
			if (!parent) {
				continue;
			}
			V target = d.rest;
			if (d.womb >= 0) {
				// carried with the cervix, along his line; the os opened and the cavity swelled on request
				V w0 = ToWorld(parent, d.rest);
				if (wombN[d.womb] && wombN[0] && wombN[2]) {
					const V&    c = wombC[d.womb];
					const V     off = Sub(w0, c);
					const float axial = Dot(off, wombAxis);
					const V     across = Sub(off, Mul(wombAxis, axial));
					const float r = Len(across);
					const V     out = r > 1e-5F ? Mul(across, 1.0F / r) : V(0, 0, 0);
					if (d.womb == 0) {
						w0 = Add(Add(c, Mul(wombAxis, axial)), Mul(out, std::max(r, osOpen.load() * scale)));
					} else if (d.womb == 1) {
						w0 = Add(Add(c, Mul(wombAxis, axial)), Mul(out, r * (1 + swell)));
					} else {
						w0 = Add(w0, Mul(wombAxis, swell * 0.5F * wombLength));
					}
				}
				target = ToLocal(parent, Add(w0, Mul(u, push)));
				d.now = Add(d.now, Mul(Sub(target, d.now), follow));
				node->local.translate = d.now;
				continue;
			}
			if (d.canal >= 0) {
				const int i = d.canal;
				if (canalOn[i]) {
					const V&    su = canalU[i];
					const V     off = Sub(ToWorld(parent, d.rest), canalC[i]);
					const float axial = Dot(off, su);
					const V     across = Sub(off, Mul(su, axial));
					const float restDist = Len(across);
					const V     out = restDist > 1e-4F ? Mul(across, 1.0F / restDist) : V(0, 0, 0);
					const float dist = std::max(restDist, canalGrow[i]);
					target = ToLocal(parent, Add(Add(Add(canalC[i], canalShift[i]), Mul(u, push * ringDepth[i])),
						Add(Mul(out, dist), Mul(su, axial))));
				} else if (push > 0) {
					target = ToLocal(parent, Add(ToWorld(parent, d.rest), Mul(u, push * ringDepth[i])));
				}
				d.now = Add(d.now, Mul(Sub(target, d.now), follow));
				node->local.translate = d.now;
				continue;
			}
			const int q = active[0] ? 0 : active[1] ? 1 : -1;   // the opening in use (only one opens)
			if (q >= 0) {
				const V&    su = segU[q];
				const V     w0 = ToWorld(parent, d.rest);
				const V     off = Sub(w0, centre[q]);
				const float axial = Dot(off, su);
				const V     across = Sub(off, Mul(su, axial));
				const float restDist = Len(across);
				const V     out = restDist > 1e-4F ? Mul(across, 1.0F / restDist) : V(0, 0, 0);
				const float g = grow[q];
				const float r0 = edgeRest[q], r1 = outerRest[q];
				float       dist = restDist;
				float       carried = 1.0F;  // how much of the ring's move across onto his line it takes
				if (d.opening == q && !d.field && !d.outer) {
					// the edge, fRimEdge from the centre on the bone's side, is what must clear him
					dist = restDist + std::max(0.0F, g - std::min(restDist, Settings::rimEdge * scale));
				} else if ((d.opening == q && !d.field) || restDist <= r1) {
					// an outer ring's, or close in: where the skin between it and the hole keeps its area, always
					// outside the inner ring
					dist = std::sqrt(restDist * restDist + std::max(0.0F, g * g - r0 * r0));
				} else {
					// further out (the other opening's rings, GT's lip and anus bones): moved as the skin there is,
					// which rides on the outer ring by the share that keeps its area, easing to none at the reach
					// (the mesh builder's RIM_PENIS and *_RIM_REACH). v0.23 left them at rest: the skin between the
					// openings, half on each's bones, hung across an open anus as a sheet
					const float lo = kReach[q][0] * scale, hi = kReach[q][1] * scale;
					const float x = std::clamp((hi - restDist) / (hi - lo), 0.0F, 1.0F);
					const float taper = x * x * (3 - 2 * x);
					const float R = kRimPenis * scale;
					auto travel = [&](float a_x, float a_g) { return std::sqrt(a_x * a_x + std::max(0.0F, a_g * a_g - r0 * r0)) - a_x; };
					const float share = taper * std::min(1.0F, travel(restDist, R) / std::max(1e-4F, travel(r1, R)));
					dist = restDist + share * travel(r1, g);
					carried = share;
				}
				const V placed = Add(Add(centre[q], Mul(shift[q], carried)), Add(Mul(out, dist), Mul(su, axial)));
				target = ToLocal(parent, placed);
			}
			d.now = Add(d.now, Mul(Sub(target, d.now), follow));
			node->local.translate = d.now;
		}
		{
			// for other plugins (SoftBodyFO4API.h)
			SoftBodyFO4API::Canal c{};
			c.version = SoftBodyFO4API::kVersion;
			auto put = [](float* a_out, const V& a_v) { a_out[0] = a_v.x; a_out[1] = a_v.y; a_out[2] = a_v.z; };
			if (canalN[0] && canalN[kVagRings - 1] && wombN[0] && wombN[2]) {
				c.herFormID = herFormID.load();
				c.ringCount = kVagRings;
				for (int i = 0; i < kVagRings; ++i) {
					const V at = canalOn[i] ? Add(Add(canalC[i], canalShift[i]), Mul(u, push * ringDepth[i])) :
					                          Add(canalC[i], Mul(u, push * ringDepth[i]));
					put(c.rings[i].centre, at);
					c.rings[i].radius = canalOn[i] ? canalGrow[i] : 0.5F * scale;
				}
				const V axis = Unit(Sub(canalC[kVagRings - 1], canalC[0]));
				put(c.cervix, Add(Add(canalC[kVagRings - 1], Mul(axis, kCervixBeyond * scale)), Mul(u, push)));
				put(c.os.centre, Add(wombC[0], Mul(u, push)));
				c.os.radius = std::max(wombR[0], osOpen.load() * scale);
				put(c.uterusCentre, Add(wombC[1], Mul(u, push)));
				put(c.uterusAxis, wombAxis);
				c.uterusLength = wombLength * (1 + 0.5F * swell);
				c.uterusRadius = wombR[1] * (1 + swell);
				c.fill = uterusFill.load();
				c.cervixPush = push;
			}
			std::scoped_lock lock{ publishedLock };
			published = c;
		}
		// the openings' paths for the snake aim: the entrance, then the canal's rest middle inward
		std::vector<V> paths[2];
		if (haveCentre[0]) {
			paths[0].push_back(aim[0]);
			for (int i = 0; i < kVagRings; ++i) {
				if (canalN[i]) paths[0].push_back(canalC[i]);
			}
			if (canalN[0] && canalN[kVagRings - 1]) {
				const V axis = Unit(Sub(canalC[kVagRings - 1], canalC[0]));
				paths[0].push_back(Add(canalC[kVagRings - 1], Mul(axis, kCervixBeyond * scale)));
			}
		}
		if (haveCentre[1]) {
			paths[1].push_back(aim[1]);
			paths[1].push_back(centre[1]);
			for (int i = kVagRings; i < kRings; ++i) {
				if (canalN[i]) paths[1].push_back(canalC[i]);
			}
		}
		BendPenis(a_dt, penis, b, u, length, paths, act);
		CalmBalls(a_dt);
	}

	// On the main thread: find her and him in the thread and hold their nodes
	void Resolve()
	{
		const auto id = followed.load();
		auto*      thread = id >= 0 && threads ? threads->getThread(id) : nullptr;
		if (thread) {
			RE::NiAVObject* her = nullptr;
			RE::NiAVObject* him = nullptr;
			Identify(thread, &her, &him);
			std::scoped_lock lock{ heldLock };
			held.clear();
			auto hold = [](const char* a_who, RE::NiAVObject* a_root, const char* a_name) {
				if (auto* node = Find(a_root, a_name)) {
					held[std::format("{}:{}", a_who, a_name)] = RE::NiPointer<RE::NiAVObject>(node);
				}
			};
			hold("her", her, "Pelvis_skin");
			for (const auto& w : kWidths) {
				hold("her", her, w.a);
				hold("her", her, w.b);
			}
			for (const auto& p : kBones) {
				hold("her", her, p.bone);
				hold("her", her, p.anchor);
			}
			for (const auto* o : { &kVagina, &kAnus }) {
				for (const auto* n : o->rim) hold("her", her, n);
				for (const auto* n : o->deep) hold("her", her, n);
			}
			for (const auto* name : kPenis) {
				hold("him", him, name);
			}
			for (const auto& p : kBendPivots) {
				hold("him", him, p.name);
			}
			for (const auto& l : bend.leaves) {
				hold("him", him, l.name);
			}
			for (const auto& ball : balls) {
				hold("him", him, ball.name);
			}
			if (her) {
				for (const auto& r : Rims()) {
					HoldRim(her, r);
				}
			}
			if (him || attempts++ % 20 == 0) {
				REX::INFO("thread {}: holding {} nodes (her {}, him {})", id, held.size(), her ? "found" : "missing", him ? "found" : "missing");
			}
			resolved = her != nullptr;
			complete = her && him;
		}
		pending = false;
	}

	void Release()
	{
		std::scoped_lock lock{ heldLock };
		held.clear();
		for (auto& [name, d] : driven) {
			if (auto* node = d.node.get()) {
				node->local.translate = d.rest;
			}
		}
		driven.clear();
		ReleaseBend();
		ReleaseBalls();
		uterusFill = 0;
		osOpen = 0;
		herFormID = 0;
		{
			std::scoped_lock plock{ publishedLock };
			published = SoftBodyFO4API::Canal{};
		}
	}

	// Off the main thread, at whatever point of the frame the timer wakes: as the SMP wireframe sees the bones, after
	// SMP has written them (read on the main thread through F4SE's task queue they showed the rest pose)
	void LogSample()
	{
		auto* pelvis = Held("her", "Pelvis_skin");
		if (!pelvis) {
			return;
		}
		const float t = std::chrono::duration<float>(Clock::now() - since).count();
		std::string line = std::format("sample,{},{:.2f},{}", samples.fetch_add(1), t, scene);
		for (const auto& w : kWidths) {
			auto* a = Held("her", w.a);
			auto* b = Held("her", w.b);
			line += a && b ? std::format(",{:.3f}", (a->world.translate - b->world.translate).Length()) : ","s;
		}
		for (const auto& p : kBones) {
			auto* bone = Held("her", p.bone);
			auto* anchor = Held("her", p.anchor);
			if (bone && anchor) {
				AppendPoint(line, ToLocal(anchor, bone->world.translate));
			} else {
				line += ",,,";
			}
		}
		for (const auto* name : kPenis) {
			if (auto* node = Held("him", name)) {
				AppendPoint(line, ToLocal(pelvis, node->world.translate));
			} else {
				line += ",,,";
			}
		}
		line += std::format(",{:.2f}", cervixPush.load());
		for (const auto& ball : balls) {      // SMP's, from where it hangs: degrees, units
			// SMP's, from where it hangs (degrees, units), and the share of writes still in place 4 ms on
			line += ball.started ? std::format(",{:.1f},{:.2f},{:.2f}", Len(ball.simTurn) * 57.2958F, Len(ball.simMove),
										   ball.wrote ? static_cast<float>(ball.kept) / static_cast<float>(ball.wrote) : 0.0F)
								 : ",,,"s;
		}
		REX::INFO("{}", line);
	}

	// ---------------------------------------------------------------- the act: which opening, which physics

	const char* ActName(Act a_act)
	{
		switch (a_act) {
		case Act::kVaginal: return "vaginal";
		case Act::kAnal: return "anal";
		case Act::kBoth: return "vaginal and anal";
		default: return "no penetration";
		}
	}

	Act SceneAct(const char* a_sceneID)
	{
		if (!scenes || !a_sceneID) {
			return Act::kNone;
		}
		bool vaginal = false, anal = false;
		for (std::uint32_t i = 0; i < scenes->getActionCount(a_sceneID); ++i) {
			const std::string type = scenes->getActionType(a_sceneID, i);
			vaginal |= type == "vaginalsex";
			anal |= type == "analsex";
		}
		return vaginal && anal ? Act::kBoth : vaginal ? Act::kVaginal : anal ? Act::kAnal : Act::kNone;
	}

	// the user picked the opening (F11's vagina or anus mode): kept, and not re-detected, until the mode changes
	std::atomic<bool> forcedAct{ false };

	// her body's current physics file, by form ID
	std::unordered_map<std::uint32_t, std::string> physics;

	// On the main thread. The file in use may not be the one last set (a reload or re-equip goes back to the NIF's), so
	// the swap is tried from each of the others; only the one in use matches.
	void SetPhysics(RE::Actor* a_her, const char* a_path)
	{
		auto  game = RE::GameVM::GetSingleton();
		auto  vm = game ? game->GetVM() : nullptr;
		auto& now = physics[a_her->GetFormID()];
		if (!vm || now == a_path) {
			return;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
		for (const auto* from : { kXmlBoth, kXmlVaginal, kXmlAnal }) {
			if (std::string_view(from) != a_path) {
				vm->DispatchStaticCall(RE::BSFixedString("DynamicHDT"), RE::BSFixedString("SwapPhysicsFile"), none,
					a_her, RE::BSFixedString(from), RE::BSFixedString(a_path), false, false);
			}
		}
		now = a_path;
		REX::INFO("her physics: {}", a_path);
	}

	void ApplyAct(FourStimAPI::Thread* a_thread, Act a_act)
	{
		if (!Settings::swapPhysics) {
			return;
		}
		if (auto* her = ActorAt(a_thread, herPosition.load())) {
			SetPhysics(her, a_act == Act::kVaginal ? kXmlVaginal : a_act == Act::kAnal ? kXmlAnal : kXmlBoth);
		}
	}

	// ---------------------------------------------------------------- alignment

	// Where the penis line crosses the opening's plane, off its centre (in her pelvis's frame, across the canal), and
	// the canal's axis. Called with heldLock held.
	std::optional<V> CrossError(const Opening& a_opening, V* a_axis)
	{
		auto* pelvis = Held("her", "Pelvis_skin");
		auto* tip = Held("him", kPenis[0]);
		auto* base = Held("him", kPenis[2]);
		if (!pelvis || !tip || !base) {
			return std::nullopt;
		}
		V rim(0, 0, 0), deep(0, 0, 0);
		for (int k = 0; k < 4; ++k) {
			auto* r = Held("her", a_opening.rim[k]);
			auto* d = Held("her", a_opening.deep[k]);
			if (!r || !d) {
				return std::nullopt;
			}
			rim = Add(rim, Mul(ToLocal(pelvis, r->world.translate), 0.25F));
			deep = Add(deep, Mul(ToLocal(pelvis, d->world.translate), 0.25F));
		}
		const V axis = Unit(Sub(deep, rim));
		rim = Sub(rim, Mul(axis, a_opening.aimOut * pelvis->world.scale));
		if (&a_opening == &kAnus && Settings::anusAimBack != 0) {
			// toward her back: from the vagina's rim centre to the anus's, across the anal canal
			V   vag(0, 0, 0);
			int vn = 0;
			for (const auto* name : kVagina.rim) {
				if (auto* r = Held("her", name)) {
					vag = Add(vag, ToLocal(pelvis, r->world.translate));
					++vn;
				}
			}
			if (vn == 4) {
				V back = Sub(rim, Mul(vag, 0.25F));
				back = Sub(back, Mul(axis, Dot(back, axis)));
				rim = Add(rim, Mul(Unit(back), Settings::anusAimBack * pelvis->world.scale));
			}
		}
		const V b = ToLocal(pelvis, base->world.translate);
		const V u = Unit(Sub(ToLocal(pelvis, tip->world.translate), b));
		const float facing = Dot(u, axis);
		if (facing < 0.3F) {  // across the opening or away from it: no crossing worth the name
			return std::nullopt;
		}
		const V cross = Add(b, Mul(u, Dot(Sub(rim, b), axis) / facing));
		V       e = Sub(cross, rim);
		e = Sub(e, Mul(axis, Dot(e, axis)));
		if (a_axis) {
			*a_axis = axis;
		}
		return e;
	}

	// How far in past the opening's rim the penis tip is (negative: short of it). Called with heldLock held.
	std::optional<float> TipDepth(const Opening& a_opening)
	{
		auto* pelvis = Held("her", "Pelvis_skin");
		auto* tip = Held("him", kPenis[0]);
		if (!pelvis || !tip) {
			return std::nullopt;
		}
		V rim(0, 0, 0), deep(0, 0, 0);
		for (int k = 0; k < 4; ++k) {
			auto* r = Held("her", a_opening.rim[k]);
			auto* d = Held("her", a_opening.deep[k]);
			if (!r || !d) {
				return std::nullopt;
			}
			rim = Add(rim, Mul(ToLocal(pelvis, r->world.translate), 0.25F));
			deep = Add(deep, Mul(ToLocal(pelvis, d->world.translate), 0.25F));
		}
		return Dot(Sub(ToLocal(pelvis, tip->world.translate), rim), Unit(Sub(deep, rim)));
	}

	// The penis's direction across the canal (the sine of its angle to it, as a vector). Called with heldLock held.
	std::optional<V> AngleError(const V& a_axis)
	{
		auto* pelvis = Held("her", "Pelvis_skin");
		auto* tip = Held("him", kPenis[0]);
		auto* base = Held("him", kPenis[2]);
		if (!pelvis || !tip || !base) {
			return std::nullopt;
		}
		const V u = Unit(Sub(ToLocal(pelvis, tip->world.translate), ToLocal(pelvis, base->world.translate)));
		return Sub(u, Mul(a_axis, Dot(u, a_axis)));
	}

	constexpr float kAngleTolerance = 6.0F;  // degrees

	enum class Phase
	{
		kOff,
		kSettle,
		kMeasure,
		kApply
	};

	// Where the penis goes, for each opening, while the act is being worked out
	struct Seen
	{
		std::vector<V>     errors;
		std::vector<V>     angles;
		std::vector<float> depths;
		V                  axis;
	};

	// the alignment's stages: centre the entrance; try a bend; set the bend; centre again
	enum class Stage
	{
		kPosition,
		kBendProbe,
		kRecentre
	};

	struct Aligner
	{
		Phase             phase{ Phase::kOff };
		const Opening*    target{ nullptr };
		Clock::time_point until;
		std::vector<V>    errors;
		V                 axis;
		M3                pelvisRotate;  // world to her pelvis, the last measured
		int               step{ 0 };
		bool              haveLast{ false };
		V                 lastError;
		V                 lastMove;
		bool              haveJ{ false };
		M3                J;  // his offset (x, y, z) -> the crossing's move in her pelvis frame
		float             first{ 0.0F };
		std::vector<V>    angles;
		Stage             stage{ Stage::kPosition };
		float             bendStart{ 0.0F };
		float             bendProbe{ 0.0F };
		V                 angleDir;            // the way the penis was off the canal's line, before the bend
		float             angleBefore{ 0.0F };  // degrees, along angleDir
		bool              detect{ false };      // working out the act from the penis first
		bool              alignAfter{ false };  // ... then aligning to it
		Seen              seenVagina, seenAnus;
	};

	std::mutex alignLock;
	Aligner    aligner;

	void StartAlign(const Opening* a_target)
	{
		std::scoped_lock lock{ alignLock };
		aligner = Aligner{};
		aligner.target = a_target;
		aligner.phase = a_target ? Phase::kSettle : Phase::kOff;
		aligner.until = Clock::now() + kSettle;
	}

	// The act from the penis: watch where it goes for a measurement, then set her physics and (a_align) align
	void StartDetect(bool a_align)
	{
		std::scoped_lock lock{ alignLock };
		aligner = Aligner{};
		aligner.detect = true;
		aligner.alignAfter = a_align;
		aligner.phase = Phase::kSettle;
		aligner.until = Clock::now() + kSettle;
	}

	const Opening* TargetFor(Act a_act);

	V Median(std::vector<V>& a_values)
	{
		auto mid = [&](auto a_get) {
			std::vector<float> c;
			c.reserve(a_values.size());
			for (const auto& v : a_values) c.push_back(a_get(v));
			std::nth_element(c.begin(), c.begin() + c.size() / 2, c.end());
			return c[c.size() / 2];
		};
		return V(mid([](const V& v) { return v.x; }), mid([](const V& v) { return v.y; }), mid([](const V& v) { return v.z; }));
	}

	// The first guess at J: 4Stim's offset taken as sideways, forward and up in his own facing
	M3 GuessJ(const M3& a_pelvisRotate)
	{
		const float h = hisHeading.load();
		const V     right(std::cos(h), -std::sin(h), 0), forward(std::sin(h), std::cos(h), 0), up(0, 0, 1);
		return a_pelvisRotate * M3::Columns(right, forward, up);
	}

	// Off the main thread, each tick: settle, measure, move him, again
	void AlignTick()
	{
		std::scoped_lock lock{ alignLock };
		auto&            a = aligner;
		if (a.phase == Phase::kOff || a.phase == Phase::kApply || Clock::now() < a.until) {
			if (a.phase == Phase::kMeasure && a.detect) {
				std::scoped_lock held_{ heldLock };
				for (auto [o, seen] : { std::pair{ &kVagina, &a.seenVagina }, std::pair{ &kAnus, &a.seenAnus } }) {
					V ax;
					auto e = CrossError(*o, &ax);
					auto d = TipDepth(*o);
					if (e && d) {
						seen->errors.push_back(*e);
						seen->depths.push_back(*d);
						seen->axis = ax;
						if (auto g = AngleError(ax)) {
							seen->angles.push_back(*g);
						}
					}
				}
				if (auto* pelvis = Held("her", "Pelvis_skin")) {
					const auto& r = pelvis->world.rotate;
					for (int i = 0; i < 3; ++i)
						for (int j = 0; j < 3; ++j) a.pelvisRotate.m[i][j] = r.entry[i][j] / pelvis->world.scale;
				}
			} else if (a.phase == Phase::kMeasure) {
				std::scoped_lock held_{ heldLock };
				V                ax;
				if (auto e = CrossError(*a.target, &ax)) {
					a.errors.push_back(*e);
					a.axis = ax;
					if (auto g = AngleError(ax)) {
						a.angles.push_back(*g);
					}
					if (auto* pelvis = Held("her", "Pelvis_skin")) {
						const auto& r = pelvis->world.rotate;
						for (int i = 0; i < 3; ++i)
							for (int j = 0; j < 3; ++j) a.pelvisRotate.m[i][j] = r.entry[i][j] / pelvis->world.scale;
					}
				}
			}
			return;
		}
		if (a.phase == Phase::kSettle) {
			a.phase = Phase::kMeasure;
			a.errors.clear();
			a.angles.clear();
			a.until = Clock::now() + kMeasure;
			return;
		}
		// a measurement is done
		if (a.detect) {
			// the opening the penis goes into: its tip mostly at or past the rim, its line the nearer of the two
			auto judge = [](Seen& a_seen, float& a_off, float& a_depth) {
				if (a_seen.errors.size() < 20) {
					return false;
				}
				std::vector<float> d = a_seen.depths;
				std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
				a_depth = d[d.size() / 2];
				a_off = Len(Median(a_seen.errors));
				return a_depth > -1.0F && a_off < 6.0F;
			};
			float      offV = 0, depthV = 0, offA = 0, depthA = 0;
			const bool v = judge(a.seenVagina, offV, depthV);
			const bool an = judge(a.seenAnus, offA, depthA);
			const Act  act = v && (!an || offV <= offA) ? Act::kVaginal : an ? Act::kAnal : Act::kNone;
			REX::INFO("act from the penis: {} (vagina: tip {:.1f} in, line {:.1f} off, {} readings; anus: tip {:.1f} in, line {:.1f} off, {} readings)",
				ActName(act), depthV, offV, a.seenVagina.errors.size(), depthA, offA, a.seenAnus.errors.size());
			currentAct = act;
			const auto threadID = followed.load();
			if (auto* tasks = F4SE::GetTaskInterface()) {
				tasks->AddTask([threadID, act]() {
					if (auto* thread = threads ? threads->getThread(threadID) : nullptr) {
						ApplyAct(thread, act);
					}
				});
			}
			a.detect = false;
			a.target = TargetFor(act);
			if (!a.alignAfter || !a.target) {
				a.phase = Phase::kOff;
				return;
			}
			// this measurement is the alignment's first
			Seen& seen = act == Act::kVaginal ? a.seenVagina : a.seenAnus;
			a.errors = std::move(seen.errors);
			a.angles = std::move(seen.angles);
			a.axis = seen.axis;
		}
		if (a.errors.size() < 20) {
			Notify(std::format("can't align: the penis doesn't point into the {} ({} usable readings)", a.target->name, a.errors.size()));
			a.phase = Phase::kOff;
			return;
		}
		const V     e = Median(a.errors);
		const float off = Len(e);
		if (a.step == 0) {
			a.first = off;
		}
		REX::INFO("align {}: step {}, penis line {:.2f} off the centre ({:.2f}, {:.2f}, {:.2f}), {} readings", a.target->name, a.step, off, e.x, e.y, e.z, a.errors.size());
		if (!a.haveJ) {
			a.J = GuessJ(a.pelvisRotate);
			a.haveJ = true;
		} else if (a.haveLast) {
			// Broyden: what the last move actually did corrects the guess
			const V seen = Sub(e, a.lastError);
			const V predicted = a.J * a.lastMove;
			const V miss = Sub(seen, predicted);
			const float dd = Dot(a.lastMove, a.lastMove);
			if (dd > 1e-6F) {
				const float d[3] = { a.lastMove.x, a.lastMove.y, a.lastMove.z };
				const float m[3] = { miss.x, miss.y, miss.z };
				for (int i = 0; i < 3; ++i)
					for (int j = 0; j < 3; ++j) a.J.m[i][j] += m[i] * d[j] / dd;
			}
		}
		const V     g = a.angles.size() >= 20 ? Median(a.angles) : V(0, 0, 0);
		const float angle = static_cast<float>(std::asin(std::min(1.0F, Len(g))) * 180.0 / 3.14159265);
		REX::INFO("align {}: penis {:.0f} degrees off the canal's line", a.target->name, angle);
		auto setBend = [&](float a_bend, bool a_probe) {
			a.phase = Phase::kApply;
			a.haveLast = false;  // the next measurement shows the bend, not a move of his: no Broyden on it
			const auto threadID = followed.load();
			const auto him = himPosition.load();
			auto*      tasks = F4SE::GetTaskInterface();
			if (!tasks || !alignment || him < 0) {
				a.phase = Phase::kOff;
				return;
			}
			tasks->AddTask([threadID, him, a_bend, a_probe]() {
				FourStimAPI::AlignmentOffset o;
				const bool ok = alignment->getOffset(threadID, static_cast<std::uint32_t>(him), &o);
				const float was = o.bend;
				// a probe goes two steps from where the bend is, away from the nearer end
				o.bend = a_probe ? (was <= 7.0F ? was + 2.0F : was - 2.0F) : a_bend;
				const bool set = ok && alignment->setOffset(threadID, static_cast<std::uint32_t>(him), &o);
				REX::INFO("align: bend {} -> {}{}", was, o.bend, set ? "" : " - failed");
				std::scoped_lock lock{ alignLock };
				if (aligner.phase == Phase::kApply) {
					if (a_probe) {
						aligner.bendStart = was;
						aligner.bendProbe = o.bend;
					}
					aligner.phase = set ? Phase::kSettle : Phase::kOff;
					aligner.until = Clock::now() + kSettle;
				}
			});
		};
		if (a.stage == Stage::kBendProbe) {
			// what two steps of bend did to the angle, along the way it was off
			const float now = static_cast<float>(std::asin(std::clamp(Dot(g, a.angleDir), -1.0F, 1.0F)) * 180.0 / 3.14159265);
			const float perStep = (now - a.angleBefore) / (a.bendProbe - a.bendStart);
			if (std::abs(perStep) < 0.5F) {
				REX::INFO("align: bend moved the angle {:.1f} degrees a step; put back", perStep);
				a.stage = Stage::kRecentre;
				a.step = 0;
				setBend(a.bendStart, false);
				return;
			}
			const float want = std::clamp(std::round(a.bendProbe - now / perStep), -9.0F, 9.0F);
			REX::INFO("align: {:.1f} degrees a bend step; bend {} for the canal's line", perStep, want);
			a.stage = Stage::kRecentre;
			a.step = 0;
			a.haveLast = false;
			setBend(want, false);
			return;
		}
		if (off <= Settings::tolerance || a.step >= Settings::maxSteps) {
			if (a.stage == Stage::kPosition && angle > kAngleTolerance && alignment && !Settings::bendPenis) {  // else BendPenis turns him
				a.angleDir = Unit(g);
				a.angleBefore = angle;
				a.stage = Stage::kBendProbe;
				setBend(0.0F, true);
				return;
			}
			if (off <= Settings::tolerance) {
				Notify(a.step == 0 && a.stage == Stage::kPosition ? std::format("{} already aligned ({:.1f} off, {:.0f} degrees)", a.target->name, off, angle) :
				                                                    std::format("aligned to the {}: {:.1f} off (was {:.1f}), {:.0f} degrees", a.target->name, off, a.first, angle));
			} else {
				Notify(std::format("couldn't align closer than {:.1f} to the {} (was {:.1f}), {:.0f} degrees", off, a.target->name, a.first, angle));
			}
			a.phase = Phase::kOff;
			return;
		}
		// the least move of his offset that takes the crossing (across the canal only) to the centre
		M3 P;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j) P.m[i][j] = (i == j ? 1.0F : 0.0F) - (&a.axis.x)[i] * (&a.axis.x)[j];
		const M3 M = P * a.J;
		M3       A = M * M.T();
		for (int i = 0; i < 3; ++i) A.m[i][i] += 1e-2F;
		V move = Mul(M.T() * (A.Inverse() * e), -1.0F);
		if (Len(move) > Settings::maxStep) {
			move = Mul(move, Settings::maxStep / Len(move));
		}
		a.lastError = e;
		a.lastMove = move;
		a.haveLast = true;
		a.phase = Phase::kApply;
		const auto threadID = followed.load();
		const auto him = himPosition.load();
		auto*      tasks = F4SE::GetTaskInterface();
		if (!tasks || !alignment || him < 0) {
			a.phase = Phase::kOff;
			return;
		}
		tasks->AddTask([threadID, him, move]() {
			FourStimAPI::AlignmentOffset o;
			const bool ok = alignment->getOffset(threadID, static_cast<std::uint32_t>(him), &o);
			if (ok) {
				o.x += move.x;
				o.y += move.y;
				o.z += move.z;
			}
			const bool set = ok && alignment->setOffset(threadID, static_cast<std::uint32_t>(him), &o);
			REX::INFO("align: moved him by ({:.2f}, {:.2f}, {:.2f}) to ({:.2f}, {:.2f}, {:.2f}){}", move.x, move.y, move.z, o.x, o.y, o.z, set ? "" : " - failed");
			std::scoped_lock lock{ alignLock };
			if (aligner.phase == Phase::kApply) {
				aligner.phase = set ? Phase::kSettle : Phase::kOff;
				aligner.until = Clock::now() + kSettle;
				aligner.step++;
			}
		});
	}

	const Opening* TargetFor(Act a_act)
	{
		return a_act == Act::kVaginal ? &kVagina : a_act == Act::kAnal ? &kAnus : nullptr;
	}

	// ---------------------------------------------------------------- keys

	bool GameInFront()
	{
		unsigned long pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &pid);
		return pid == GetCurrentProcessId();
	}

	// F11 steps through the modes: auto (the scene's act, or the penis's), the vagina, the anus, off
	enum class Mode
	{
		kAuto,
		kVagina,
		kAnus,
		kOff
	};
	std::atomic<Mode> mode{ Mode::kAuto };
	std::atomic<Act>  sceneAct{ Act::kNone };  // from 4Stim's Scenes interface, when it has one

	const char* ModeName(Mode a_mode)
	{
		switch (a_mode) {
		case Mode::kAuto: return "auto";
		case Mode::kVagina: return "the vagina";
		case Mode::kAnus: return "the anus";
		default: return "off";
		}
	}

	// her physics for an act, on the main thread
	void ApplyActLater(Act a_act)
	{
		const auto threadID = followed.load();
		if (auto* tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask([threadID, a_act]() {
				if (auto* thread = threads ? threads->getThread(threadID) : nullptr) {
					ApplyAct(thread, a_act);
				}
			});
		}
	}

	// the mode taken up: at a key press, or a scene change (a_thread on the main thread, else null)
	void EnterMode(Mode a_mode, FourStimAPI::Thread* a_thread)
	{
		auto apply = [&](Act a_act) {
			if (a_thread) {
				ApplyAct(a_thread, a_act);
			} else {
				ApplyActLater(a_act);
			}
		};
		forcedAct = a_mode == Mode::kVagina || a_mode == Mode::kAnus;
		switch (a_mode) {
		case Mode::kVagina:
		case Mode::kAnus:
			{
				const Act act = a_mode == Mode::kVagina ? Act::kVaginal : Act::kAnal;
				currentAct = act;
				apply(act);
				StartAlign(TargetFor(act));
				break;
			}
		case Mode::kAuto:
			if (!scenes) {  // from where the penis goes, after a moment
				currentAct = Act::kNone;
				StartDetect(true);
			} else {
				currentAct = sceneAct.load();
				apply(currentAct);
				StartAlign(TargetFor(currentAct));
			}
			break;
		case Mode::kOff:
			StartAlign(nullptr);
			if (!scenes) {  // her physics by act still, without aligning
				currentAct = Act::kNone;
				if (Settings::swapPhysics) {
					StartDetect(false);
				}
			} else {
				currentAct = sceneAct.load();
				apply(currentAct);
			}
			break;
		}
	}

	void Keys()
	{
		static bool wasDown = false;
		const bool  down = (GetAsyncKeyState(Settings::alignKey) & 0x8000) != 0;
		const bool  pressed = down && !wasDown;
		wasDown = down;
		if (!pressed || !GameInFront() || followed.load() < 0) {
			return;
		}
		const bool ctrl = (GetAsyncKeyState(0x11) & 0x8000) != 0;
		if (ctrl) {
			StartAlign(nullptr);
			const auto threadID = followed.load();
			const auto him = himPosition.load();
			if (auto* tasks = F4SE::GetTaskInterface(); tasks && alignment && him >= 0) {
				tasks->AddTask([threadID, him]() {
					alignment->resetOffset(threadID, static_cast<std::uint32_t>(him));
				});
				Notify("back to the scene's own alignment");
			}
			return;
		}
		const Mode next = static_cast<Mode>((static_cast<int>(mode.load()) + 1) % 4);
		mode = next;
		Notify(std::format("align: {}", ModeName(next)));
		EnterMode(next, nullptr);
	}

	// ---------------------------------------------------------------- the timer

	void Timer(std::stop_token a_stop)
	{
		while (!a_stop.stop_requested()) {
			std::this_thread::sleep_for(kInterval);
			Keys();
			if (followed.load() < 0) {
				continue;
			}
			// her first; him may not be loaded yet at scene start, so he is looked for again about once a second
			static int tick = 0;
			if (!resolved || (!complete && ++tick % 40 == 0)) {
				if (!pending.exchange(true)) {
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask(Resolve);
					} else {
						pending = false;
					}
				}
				if (!resolved) {
					continue;
				}
			}
			AlignTick();
			if (Settings::logSamples) {
				std::scoped_lock lock{ heldLock };
				LogSample();
			}
		}
	}

	std::jthread timer;

	// the rims, faster than the timer: a frame or so behind the penis
	void Driver(std::stop_token a_stop)
	{
		auto last = Clock::now();
		while (!a_stop.stop_requested()) {
			std::this_thread::sleep_for(4ms);
			const auto  now = Clock::now();
			const float dt = std::chrono::duration<float>(now - last).count();
			last = now;
			if (followed.load() < 0 || !resolved) {
				continue;
			}
			std::scoped_lock lock{ heldLock };
			DriveRims(dt);
		}
	}

	std::jthread driver;

	// ---------------------------------------------------------------- 4Stim's events (main thread)

	void OnScene(FourStimAPI::Thread* a_thread)
	{
		const char* id = a_thread->getCurrentSceneID();
		{
			std::scoped_lock lock{ heldLock };
			scene = id ? id : "";
		}
		Identify(a_thread, nullptr, nullptr);
		const Act act = scenes ? SceneAct(id) : Act::kNone;
		sceneAct = act;
		REX::INFO("thread {} in {}: {}, align {}", a_thread->getThreadID(), id ? id : "?",
			scenes ? ActName(act) : "act from the penis", ModeName(mode.load()));
		if (!scenes && mode.load() == Mode::kOff && !Settings::swapPhysics) {
			currentAct = Act::kNone;
			return;
		}
		EnterMode(mode.load(), a_thread);
	}

	struct ThreadStarted : FourStimAPI::ThreadEventListener
	{
		void listen(FourStimAPI::Thread* a_thread) override
		{
			samples = 0;
			since = Clock::now();
			resolved = false;
			complete = false;
			attempts = 0;
			followed = a_thread->getThreadID();
			REX::INFO("following 4Stim thread {} with {} actor(s)", a_thread->getThreadID(), a_thread->getActorCount());
			if (Settings::logSamples) {
				REX::INFO("{}", Header());
			}
			OnScene(a_thread);
		}
	};

	struct SceneChanged : FourStimAPI::ThreadEventListener
	{
		void listen(FourStimAPI::Thread* a_thread) override
		{
			if (a_thread->getThreadID() == followed.load()) {
				OnScene(a_thread);
			}
		}
	};

	struct ThreadStopped : FourStimAPI::ThreadEventListener
	{
		void listen(FourStimAPI::Thread* a_thread) override
		{
			if (a_thread->getThreadID() != followed.load()) {
				return;
			}
			StartAlign(nullptr);
			ApplyAct(a_thread, Act::kNone);  // her full physics back
			followed = -1;
			currentAct = Act::kNone;
			REX::INFO("thread {} over after {} samples", a_thread->getThreadID(), samples.load());
			Release();
		}
	};

	ThreadStarted started;
	SceneChanged  changed;
	ThreadStopped stopped;

	void Connect()
	{
		FourStimAPI::InterfaceExchangeMessage message;
		if (auto* messaging = F4SE::GetMessagingInterface()) {
			messaging->Dispatch(FourStimAPI::InterfaceExchangeMessage::MESSAGE_TYPE, &message, sizeof(message), "4Stim");
		}
		if (!message.interfaceMap) {
			REX::WARN("4Stim not found: nothing to do");
			return;
		}
		auto* map = message.interfaceMap;
		threads = static_cast<FourStimAPI::ThreadInterface*>(map->queryInterface(FourStimAPI::ThreadInterface::NAME));
		alignment = static_cast<FourStimAPI::AlignmentInterface*>(map->queryInterface(FourStimAPI::AlignmentInterface::NAME));
		scenes = static_cast<FourStimAPI::SceneInterface*>(map->queryInterface(FourStimAPI::SceneInterface::NAME));
		if (!threads) {
			REX::WARN("4Stim has no thread interface: nothing to do");
			return;
		}
		threads->registerThreadStartListener(&started);
		threads->registerNodeChangedListener(&changed);
		threads->registerThreadStopListener(&stopped);
		REX::INFO("following 4Stim's scenes (threads v{}, alignment {}, scenes {})", threads->getVersion(),
			alignment ? "yes" : "no - no auto-align", scenes ? "yes" : "no - the act comes from where the penis goes");
	}

	void OnMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (a_message->type == F4SE::MessagingInterface::kPostLoad) {
			Connect();
		}
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);
	REX::INFO("SoftBodyFO4 loaded on Fallout 4 {}", a_f4se->RuntimeVersion().string());
	Settings::Load();
	mode = Settings::autoAlign ? Mode::kAuto : Mode::kOff;
	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		REX::ERROR("could not register the F4SE message listener");
		return false;
	}
	timer = std::jthread(Timer);
	driver = std::jthread(Driver);
	return true;
}

// ---------------------------------------------------------------- the interface for other plugins (SoftBodyFO4API.h)
extern "C" __declspec(dllexport) bool SoftBodyFO4_GetCanal(SoftBodyFO4API::Canal* a_out)
{
	if (!a_out) {
		return false;
	}
	std::scoped_lock lock{ publishedLock };
	*a_out = published;
	a_out->version = SoftBodyFO4API::kVersion;
	return published.herFormID != 0;
}

extern "C" __declspec(dllexport) void SoftBodyFO4_SetUterusFill(std::uint32_t a_her, float a_fill)
{
	if (a_her && a_her == herFormID.load()) {
		uterusFill = std::clamp(a_fill, 0.0F, 1.0F);
	}
}

extern "C" __declspec(dllexport) void SoftBodyFO4_SetOsOpen(std::uint32_t a_her, float a_radius)
{
	if (a_her && a_her == herFormID.load()) {
		osOpen = std::max(0.0F, a_radius);
	}
}

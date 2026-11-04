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
		float penisRadius = 1.8F;     // the shaft's
		float penisTipLength = 1.5F;  // the rounded tip: the radius grows from 0 at its end to the shaft's over this
		float penisTipExtra = 1.0F;   // the penis's end beyond its last bone (Penis_05_SMP)
		float rimSmoothing = 0.02F;   // seconds: how quickly the rims follow
		float rimEdge = 0.5F;         // an opening's edge at rest, from its centre: where a rim bone's skin starts

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
			penisTipLength = std::max(0.1F, f("fPenisTipLength", penisTipLength));
			penisTipExtra = f("fPenisTipExtra", penisTipExtra);
			rimSmoothing = std::max(0.0F, f("fRimSmoothing", rimSmoothing));
			rimEdge = std::max(0.0F, f("fRimEdge", rimEdge));
			REX::INFO("settings: openings driven {}, penis radius {}, tip length {}, tip beyond its bone {}, rim smoothing {}s",
				driveOpenings, penisRadius, penisTipLength, penisTipExtra, rimSmoothing);
			REX::INFO("settings: swap physics {}, auto-align {}, log samples {}, align key {:#x}, tolerance {}, {} steps of up to {}",
				swapPhysics, autoAlign, logSamples, alignKey, tolerance, maxSteps, maxStep);
		}
	}

	// ---------------------------------------------------------------- small vector maths

	float Dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
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
		{ "vagRim_FB", "SB VagRim_0", "SB VagRim_6" }, { "vagRim_LR", "SB VagRim_3", "SB VagRim_9" },
		{ "anusRim_TB", "SB AnusRim_0", "SB AnusRim_6" }, { "anusRim_LR", "SB AnusRim_3", "SB AnusRim_9" },
		{ "vagIn_FB", "SB VagIn_0", "SB VagIn_6" }, { "vagIn_LR", "SB VagIn_3", "SB VagIn_9" },
		{ "vag1_FB", "SB Vag1_0", "SB Vag1_6" }, { "vag1_LR", "SB Vag1_3", "SB Vag1_9" },
		{ "vag2_FB", "SB Vag2_0", "SB Vag2_6" }, { "vag2_LR", "SB Vag2_3", "SB Vag2_9" },
		{ "vag3_FB", "SB Vag3_0", "SB Vag3_6" }, { "vag3_LR", "SB Vag3_3", "SB Vag3_9" },
		{ "vag4_FB", "SB Vag4_0", "SB Vag4_6" }, { "vag4_LR", "SB Vag4_3", "SB Vag4_9" },
		{ "vag5_FB", "SB Vag5_0", "SB Vag5_6" }, { "vag5_LR", "SB Vag5_3", "SB Vag5_9" },
	};

	// A-Body's penis chain: tip, middle, base (the _SMP bones are the simulated ones)
	constexpr const char* kPenis[] = { "Penis_05_SMP", "Penis_03_SMP", "Penis_01_SMP" };

	// The openings, from her kinematic anchors (they hang still off her pelvis): the centre of the rim, and a point
	// down the canal for its axis
	struct Opening
	{
		const char*                name;
		std::array<const char*, 4> rim;
		std::array<const char*, 4> deep;
	};
	// each: its visible rim's ring (the vagina's at the back of the slit, where a real entrance is) and a canal ring
	constexpr Opening kVagina{ "vagina", { "SB VagRim_0 Base", "SB VagRim_3 Base", "SB VagRim_6 Base", "SB VagRim_9 Base" },
		{ "SB Vag2_0 Base", "SB Vag2_3 Base", "SB Vag2_6 Base", "SB Vag2_9 Base" } };
	constexpr Opening kAnus{ "anus", { "SB AnusRim_0 Base", "SB AnusRim_3 Base", "SB AnusRim_6 Base", "SB AnusRim_9 Base" },
		{ "SB AnusT2 Base", "SB AnusB2 Base", "SB AnusL2 Base", "SB AnusR2 Base" } };

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
		return header;
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
	void Identify(FourStimAPI::Thread* a_thread, RE::NiAVObject** a_her, RE::NiAVObject** a_him)
	{
		std::int32_t herAt = -1, himAt = -1;
		for (std::uint32_t i = 0; i < a_thread->getActorCount(); ++i) {
			auto* actor = ActorAt(a_thread, static_cast<std::int32_t>(i));
			auto* root = actor ? actor->Get3D() : nullptr;
			if (herAt < 0 && Find(root, "VaginaDB02")) {
				herAt = static_cast<std::int32_t>(i);
				if (a_her) *a_her = root;
			} else if (himAt < 0 && Find(root, kPenis[0])) {
				himAt = static_cast<std::int32_t>(i);
				if (a_him) *a_him = root;
				hisHeading = actor->data.angle.z;
			}
		}
		herPosition = herAt;
		himPosition = himAt;
	}

	// ---------------------------------------------------------------- the openings' rims, round the penis
	// Left to collision, the rims opened where the thrusting penis shoved their pads: along the slit, lagging, oval.
	// So her physics files make the rims' bones kinematic, and here each is pushed straight out from the penis's axis
	// to its radius, from where it rests, wherever the penis (shaft and rounded tip) passes within that radius of it.
	// The skin on them opens round the shaft and closes behind it. Nothing else of hers is moved.
	struct Rim
	{
		const char* name;
		int         opening;  // 0 the vagina, 1 the anus: its centre gives the way out, and the edge's place
		float       share{ 1.0F };  // of its edge's move: the hood (Clitoral1) barely lifts
	};
	constexpr Rim kRims[] = {
		{ "Clitoral1", 0, 0.3F }, { "VaginaB1", 0 }, { "NPC L Pussy02", 0 }, { "NPC R Pussy02", 0 },
		{ "SB VagIn_0", 0 }, { "SB VagIn_1", 0 }, { "SB VagIn_2", 0 }, { "SB VagIn_3", 0 }, { "SB VagIn_4", 0 }, { "SB VagIn_5", 0 },
		{ "SB VagIn_6", 0 }, { "SB VagIn_7", 0 }, { "SB VagIn_8", 0 }, { "SB VagIn_9", 0 }, { "SB VagIn_10", 0 }, { "SB VagIn_11", 0 },
		{ "NPC LT Anus2", 1 }, { "NPC RT Anus2", 1 }, { "NPC LB Anus2", 1 }, { "NPC RB Anus2", 1 },
		// the visible rims: 12 bones round each hole, at the skin
		{ "SB VagRim_0", 0 }, { "SB VagRim_1", 0 }, { "SB VagRim_2", 0 }, { "SB VagRim_3", 0 }, { "SB VagRim_4", 0 }, { "SB VagRim_5", 0 }, { "SB VagRim_6", 0 }, { "SB VagRim_7", 0 }, { "SB VagRim_8", 0 }, { "SB VagRim_9", 0 }, { "SB VagRim_10", 0 }, { "SB VagRim_11", 0 },
		{ "SB AnusRim_0", 1 }, { "SB AnusRim_1", 1 }, { "SB AnusRim_2", 1 }, { "SB AnusRim_3", 1 }, { "SB AnusRim_4", 1 }, { "SB AnusRim_5", 1 }, { "SB AnusRim_6", 1 }, { "SB AnusRim_7", 1 }, { "SB AnusRim_8", 1 }, { "SB AnusRim_9", 1 }, { "SB AnusRim_10", 1 }, { "SB AnusRim_11", 1 },
	};

	struct Driven
	{
		RE::NiPointer<RE::NiAVObject> node;
		V                             rest;  // its own local position, as the NIF has it
		V                             now;
		int                           opening{ 0 };
		float                         share{ 1.0F };
	};
	std::unordered_map<std::string, Driven> driven;  // under heldLock

	// on the main thread, from Resolve: a rim bone's rest is taken once, when it is first seen
	void HoldRim(RE::NiAVObject* a_her, const Rim& a_rim)
	{
		auto* node = Find(a_her, a_rim.name);
		if (!node) {
			return;
		}
		auto& d = driven[a_rim.name];
		if (d.node.get() != node) {
			d = Driven{ RE::NiPointer<RE::NiAVObject>(node), node->local.translate, node->local.translate, a_rim.opening, a_rim.share };
			REX::INFO("rim {}: rest ({:.3f}, {:.3f}, {:.3f})", a_rim.name, d.rest.x, d.rest.y, d.rest.z);
		}
	}

	// off the main thread, every few milliseconds, under heldLock
	void DriveRims(float a_dt)
	{
		auto*      tip = Held("him", kPenis[0]);
		auto*      base = Held("him", kPenis[2]);
		const bool penis = Settings::driveOpenings && tip && base;
		V          b(0, 0, 0), u(0, 0, 0);
		float      length = 0, scale = 1;
		if (penis) {
			b = base->world.translate;
			const V t = tip->world.translate;
			u = Unit(Sub(t, b));
			scale = tip->world.scale;
			length = Len(Sub(t, b)) + Settings::penisTipExtra * scale;
		}
		// each opening's centre, from its kinematic rim anchors
		V    centre[2];
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
		}
		const float follow = Settings::rimSmoothing > 0 ? 1.0F - std::exp(-a_dt / Settings::rimSmoothing) : 1.0F;
		const float radius = Settings::penisRadius * scale;
		const float tipLength = Settings::penisTipLength * scale;
		for (auto& [name, d] : driven) {
			auto* node = d.node.get();
			auto* parent = node ? node->parent : nullptr;
			if (!parent) {
				continue;
			}
			V target = d.rest;
			if (penis) {
				// A bone can sit well out from the edge of the opening it carries (GT's lip bones are about 3 from the
				// slit's middle, the slit itself closed), so it is the edge that is measured against the penis: the
				// point on the bone's side of the opening, fRimEdge from its centre, at the bone's depth. The bone
				// moves as that edge must.
				const V w0 = ToWorld(parent, d.rest);
				V       out(0, 0, 0), edge = w0;
				if (haveCentre[d.opening]) {
					V           off = Sub(w0, centre[d.opening]);
					const float restDist = Len(Sub(off, Mul(u, Dot(off, u))));
					out = Unit(Sub(off, Mul(u, Dot(off, u))));
					edge = Sub(w0, Mul(out, std::max(0.0F, restDist - Settings::rimEdge * scale)));
				}
				const V     rel = Sub(edge, b);
				const float along = Dot(rel, u);
				const float fromTip = length - along;
				if (along > 0 && fromTip > 0) {
					const float r = fromTip >= tipLength ? radius : radius * std::sqrt(fromTip / tipLength);
					const V     perp = Sub(rel, Mul(u, along));
					const float dist = Len(perp);
					if (dist < r) {
						// out from the axis; a touch of the way out from the opening's centre keeps an edge the axis
						// runs through from flipping sides
						const V dir = Unit(Add(perp, Mul(out, 0.05F)));
						if (Len(dir) > 0) {
							target = ToLocal(parent, Add(w0, Mul(dir, (r - dist) * d.share)));
						}
					}
				}
			}
			d.now = Add(d.now, Mul(Sub(target, d.now), follow));
			node->local.translate = d.now;
		}
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
			if (her) {
				for (const auto& r : kRims) {
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
		REX::INFO("{}", line);
	}

	// ---------------------------------------------------------------- the act: which opening, which physics

	enum class Act
	{
		kNone,
		kVaginal,
		kAnal,
		kBoth
	};

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

	std::atomic<Act> currentAct{ Act::kNone };
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
			if (a.stage == Stage::kPosition && angle > kAngleTolerance && alignment) {
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

// SoftBodyFO4: SoftBody FO4's F4SE plugin. In each 4Stim scene it
//  1. gives her body the physics for the scene's act, as GT Softbody switches its collision by act: for vaginal sex
//     SoftBody-CBBE-Vaginal.xml, for anal -Anal.xml, otherwise the full SoftBody-CBBE.xml (FO4 FSMP's DynamicHDT).
//     The opening not in use lets the penis through, so it can't catch it or be dragged about by it.
//  2. aligns the penis with the opening in use. GT gets there with a bendable penis that her collision steers in;
//     4Stim's penis is rigid, so his 4Stim alignment offset is corrected instead (4Stim saves it with the scene):
//     where the penis line crosses the opening is measured over a couple of seconds of the loop, he is moved, and it
//     is measured again, until it is within the tolerance.
//  3. optionally logs her genital bones and the penis (what was SoftBodyProbe), for tuning.
// Settings: Data\F4SE\Plugins\SoftBodyFO4.ini. Keys, with the game in front: F10 align now, Shift+F10 auto-align on
// or off, Ctrl+F10 back to the scene's own alignment.

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
		int   alignKey = 0x79;  // F10
		float tolerance = 0.4F;  // game units: the penis line within this of the opening's centre is aligned
		int   maxSteps = 5;
		float maxStep = 6.0F;  // the most he is moved in one step

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
			autoAlign = i("bAutoAlign", 1) != 0;
			logSamples = i("bLogSamples", 0) != 0;
			alignKey = i("iAlignKey", alignKey);
			tolerance = f("fAlignTolerance", tolerance);
			maxSteps = i("iAlignMaxSteps", maxSteps);
			maxStep = f("fAlignMaxStep", maxStep);
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
	// the vagina: its entrance ring (at the back of the slit, where a real entrance is) and the canal's second ring
	constexpr Opening kVagina{ "vagina", { "SB VagIn_0 Base", "SB VagIn_3 Base", "SB VagIn_6 Base", "SB VagIn_9 Base" },
		{ "SB Vag2_0 Base", "SB Vag2_3 Base", "SB Vag2_6 Base", "SB Vag2_9 Base" } };
	constexpr Opening kAnus{ "anus", { "NPC LT Anus1", "NPC RT Anus1", "NPC LB Anus1", "NPC RB Anus1" },
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

	enum class Phase
	{
		kOff,
		kSettle,
		kMeasure,
		kApply
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
			if (a.phase == Phase::kMeasure) {
				std::scoped_lock held_{ heldLock };
				V                ax;
				if (auto e = CrossError(*a.target, &ax)) {
					a.errors.push_back(*e);
					a.axis = ax;
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
			a.until = Clock::now() + kMeasure;
			return;
		}
		// a measurement is done
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
		if (off <= Settings::tolerance) {
			Notify(a.step == 0 ? std::format("{} already aligned ({:.1f} off)", a.target->name, off) :
			                     std::format("aligned to the {}: {:.1f} off, was {:.1f}", a.target->name, off, a.first));
			a.phase = Phase::kOff;
			return;
		}
		if (a.step >= Settings::maxSteps) {
			Notify(std::format("couldn't align closer than {:.1f} to the {} (was {:.1f})", off, a.target->name, a.first));
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

	void Keys()
	{
		static bool wasDown = false;
		const bool  down = (GetAsyncKeyState(Settings::alignKey) & 0x8000) != 0;
		const bool  pressed = down && !wasDown;
		wasDown = down;
		if (!pressed || !GameInFront() || followed.load() < 0) {
			return;
		}
		const bool shift = (GetAsyncKeyState(0x10) & 0x8000) != 0;
		const bool ctrl = (GetAsyncKeyState(0x11) & 0x8000) != 0;
		if (shift) {
			Settings::autoAlign = !Settings::autoAlign;
			Notify(std::format("auto-align {}", Settings::autoAlign ? "on" : "off"));
		} else if (ctrl) {
			StartAlign(nullptr);
			const auto threadID = followed.load();
			const auto him = himPosition.load();
			if (auto* tasks = F4SE::GetTaskInterface(); tasks && alignment && him >= 0) {
				tasks->AddTask([threadID, him]() {
					alignment->resetOffset(threadID, static_cast<std::uint32_t>(him));
				});
				Notify("back to the scene's own alignment");
			}
		} else if (auto* target = TargetFor(currentAct.load())) {
			Notify(std::format("aligning to the {}", target->name));
			StartAlign(target);
		} else {
			Notify("nothing to align: this scene has no vaginal or anal sex");
		}
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

	// ---------------------------------------------------------------- 4Stim's events (main thread)

	void OnScene(FourStimAPI::Thread* a_thread)
	{
		const char* id = a_thread->getCurrentSceneID();
		{
			std::scoped_lock lock{ heldLock };
			scene = id ? id : "";
		}
		Identify(a_thread, nullptr, nullptr);
		const Act act = SceneAct(id);
		currentAct = act;
		REX::INFO("thread {} in {}: {}", a_thread->getThreadID(), id ? id : "?", ActName(act));
		ApplyAct(a_thread, act);
		StartAlign(Settings::autoAlign ? TargetFor(act) : nullptr);
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
			alignment ? "yes" : "no - no auto-align", scenes ? "yes" : "no - no per-act physics or auto-align");
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
	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		REX::ERROR("could not register the F4SE message listener");
		return false;
	}
	timer = std::jthread(Timer);
	return true;
}

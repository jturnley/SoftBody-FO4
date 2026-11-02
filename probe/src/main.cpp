// SoftBodyProbe: while a 4Stim scene runs, ten times a second logs (to SoftBodyProbe.log in the F4SE log folder)
// where SoftBody FO4's genital bones are and where the penis is. Temporary, for tuning the openings.
//
// Each "sample" line is CSV (the header line names the columns):
//   n, t (s), scene, then
//   widths    the opening widths, node to node (game units)
//   <bone>    each moving genital bone in its anchor's frame (x, y, z): the spring's offset, as its limits see it
//   <penis>   the penis tip, middle and base in her Pelvis_skin frame (x, y, z)

#include "PluginInterface.h"

namespace
{
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
	};

	// A-Body's penis chain: tip, middle, base (the _SMP bones are the simulated ones)
	constexpr const char* kPenis[] = { "Penis_05_SMP", "Penis_03_SMP", "Penis_01_SMP" };

	constexpr auto kInterval = 100ms;

	FourStimAPI::ThreadInterface*         threads{ nullptr };
	std::atomic<std::int32_t>             followed{ -1 };
	std::atomic_bool                      pending{ false };
	std::chrono::steady_clock::time_point since;
	std::int32_t                          samples{ 0 };

	RE::NiAVObject* Find(RE::NiAVObject* a_root, const char* a_name)
	{
		return a_root ? a_root->GetObjectByName(RE::BSFixedString(a_name)) : nullptr;
	}

	// a world point in a node's own frame. FO4 keeps NiMatrix3 transposed from what NiTransform's operator* assumes
	// (4Strands' BodyProbe found the same with skin-to-bone), so the stored matrix itself maps world to local
	RE::NiPoint3 ToLocal(const RE::NiAVObject* a_node, const RE::NiPoint3& a_world)
	{
		const auto& w = a_node->world;
		return (w.rotate * (a_world - w.translate)) / w.scale;
	}

	void Append(std::string& a_line, const RE::NiPoint3& a_point)
	{
		a_line += std::format(",{:.3f},{:.3f},{:.3f}", a_point.x, a_point.y, a_point.z);
	}

	void AppendMissing(std::string& a_line, int a_count)
	{
		for (int i = 0; i < a_count; ++i) {
			a_line += ",";
		}
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

	void Sample(FourStimAPI::Thread* a_thread)
	{
		RE::NiAVObject* her = nullptr;
		RE::NiAVObject* him = nullptr;
		for (std::uint32_t i = 0; i < a_thread->getActorCount(); ++i) {
			auto* actor = a_thread->getActor(i) ? static_cast<RE::Actor*>(a_thread->getActor(i)->getGameActor()) : nullptr;
			auto* root = actor ? actor->Get3D() : nullptr;
			if (!her && Find(root, "VaginaDB02")) {
				her = root;
			} else if (!him && Find(root, kPenis[0])) {
				him = root;
			}
		}
		auto* pelvis = Find(her, "Pelvis_skin");
		if (!pelvis) {
			if (samples++ == 0) {
				REX::INFO("no actor in thread {} carries the SoftBody genital bones", a_thread->getThreadID());
			}
			return;
		}
		const char* scene = a_thread->getCurrentSceneID();
		const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - since).count();
		std::string line = std::format("sample,{},{:.2f},{}", samples++, t, scene ? scene : "");
		for (const auto& w : kWidths) {
			auto* a = Find(her, w.a);
			auto* b = Find(her, w.b);
			line += a && b ? std::format(",{:.3f}", (a->world.translate - b->world.translate).Length()) : ","s;
		}
		for (const auto& p : kBones) {
			auto* bone = Find(her, p.bone);
			auto* anchor = Find(her, p.anchor);
			if (bone && anchor) {
				Append(line, ToLocal(anchor, bone->world.translate));
			} else {
				AppendMissing(line, 3);
			}
		}
		for (const auto* name : kPenis) {
			if (auto* node = Find(him, name)) {
				Append(line, ToLocal(pelvis, node->world.translate));
			} else {
				AppendMissing(line, 3);
			}
		}
		REX::INFO("{}", line);
	}

	// One sample, on the main thread (queued by the timer below)
	void SampleNow()
	{
		const auto id = followed.load();
		if (id >= 0 && threads) {
			auto* thread = threads->getThread(id);
			if (!thread || !thread->isRunning()) {
				REX::INFO("thread {} over after {} samples", id, samples);
				followed = -1;
			} else {
				Sample(thread);
			}
		}
		pending = false;
	}

	// Every kInterval while a scene is followed, queues a sample onto the main thread through F4SE's task interface
	// (PlayerCharacter::Update is no frame clock: it stops while 4Stim drives the player)
	void Timer(std::stop_token a_stop)
	{
		while (!a_stop.stop_requested()) {
			std::this_thread::sleep_for(kInterval);
			if (followed.load() >= 0 && !pending.exchange(true)) {
				if (auto* tasks = F4SE::GetTaskInterface()) {
					tasks->AddTask(SampleNow);
				} else {
					pending = false;
				}
			}
		}
	}

	std::jthread timer;

	struct ThreadStarted : FourStimAPI::ThreadEventListener
	{
		void listen(FourStimAPI::Thread* a_thread) override
		{
			samples = 0;
			since = std::chrono::steady_clock::now();
			followed = a_thread->getThreadID();
			REX::INFO("following 4Stim thread {} in {} with {} actor(s)", a_thread->getThreadID(), a_thread->getCurrentSceneID(), a_thread->getActorCount());
			REX::INFO("{}", Header());
		}
	};

	struct SceneChanged : FourStimAPI::ThreadEventListener
	{
		void listen(FourStimAPI::Thread* a_thread) override
		{
			REX::INFO("thread {} now in {}", a_thread->getThreadID(), a_thread->getCurrentSceneID());
		}
	};

	ThreadStarted started;
	SceneChanged  changed;

	void Connect()
	{
		FourStimAPI::InterfaceExchangeMessage message;
		if (auto* messaging = F4SE::GetMessagingInterface()) {
			messaging->Dispatch(FourStimAPI::InterfaceExchangeMessage::MESSAGE_TYPE, &message, sizeof(message), "4Stim");
		}
		if (!message.interfaceMap) {
			REX::WARN("4Stim not found: nothing to follow");
			return;
		}
		threads = static_cast<FourStimAPI::ThreadInterface*>(message.interfaceMap->queryInterface(FourStimAPI::ThreadInterface::NAME));
		if (!threads) {
			REX::WARN("4Stim has no thread interface: nothing to follow");
			return;
		}
		threads->registerThreadStartListener(&started);
		threads->registerNodeChangedListener(&changed);
		REX::INFO("following 4Stim's scenes (thread interface version {})", threads->getVersion());
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
	REX::INFO("SoftBodyProbe loaded on Fallout 4 {}", a_f4se->RuntimeVersion().string());
	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		REX::ERROR("could not register the F4SE message listener");
		return false;
	}
	timer = std::jthread(Timer);
	return true;
}

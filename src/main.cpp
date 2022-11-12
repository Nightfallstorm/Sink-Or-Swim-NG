extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "SinkOrSwim";
	a_info->version = 1;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	return true;
}

void InitializeLog()
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return;
	}

	*path /= "SinkOrSwim.log"sv;
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info("SinkOrSwim v1.0.0");
}

static bool isHeavy = FALSE;

class Loki_SinkOrSwim
{
public:
	void* CodeAllocation(Xbyak::CodeGenerator& a_code, SKSE::Trampoline* t_ptr)
	{
		auto result = t_ptr->allocate(a_code.getSize());
		std::memcpy(result, a_code.getCode(), a_code.getSize());
		return result;
	}
	static void InstallSwimmingHook()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(36357, 37348), REL::VariantOffset(0x6ED, 0x68E, 0x6ED) };	 // actor_update code insertion point
		REL::Relocation<std::uintptr_t> isSwimmingVariable{ RELOCATION_ID(241932, 195872) };
		Loki_SinkOrSwim L_SOS;

		struct Patch : Xbyak::CodeGenerator
		{
			Patch(std::uintptr_t a_variable, std::uintptr_t a_target)
			{
				if (REL::Module::IsAE()) {
					Xbyak::Label _OurReturn;
					Xbyak::Label retLabel;
					Xbyak::Label isSwimmingAddr;

					setae(sil);
					mov(rcx, (uintptr_t)&isHeavy);
					cmp(byte[rcx], 1);
					jne(retLabel);
					mov(sil, 0);  // set kIsSwimming to 0 depending on if our own isHeavy is set to TRUE

					L(retLabel);
					mov(ptr[rbp + 0x1D0], sil);	 // this is the games addr for checking if we are at the drowning level
					jmp(ptr[rip + _OurReturn]);	 // we do not touch the drowning flag or this address, but we overwrite
												 // part of the code so we need to bring it into our injection
					L(isSwimmingAddr);			 // all of this happens directly AFTER GetSubmergedLevel()
					dq(a_variable);

					L(_OurReturn);
					dq(a_target + 0xB);	 // 0x68E - 0x699
				} else {
					Xbyak::Label _OurReturn;
					Xbyak::Label retLabel;
					Xbyak::Label isSwimmingAddr;

					setae(r13b);
					mov(rcx, (uintptr_t)&isHeavy);
					cmp(byte[rcx], 1);
					jne(retLabel);
					mov(r13b, 0);  // set kIsSwimming to 0 depending on if our own isHeavy is set to TRUE

					L(retLabel);
					mov(rcx, ptr[rip + isSwimmingAddr]);
					comiss(xmm6, ptr[rcx]);		 // this is the games addr for checking if we are at the drowning level
					jmp(ptr[rip + _OurReturn]);	 // we do not touch the drowning flag or this address, but we overwrite
												 // part of the code so we need to bring it into our injection
					L(isSwimmingAddr);			 // all of this happens directly AFTER GetSubmergedLevel()
					dq(a_variable);

					L(_OurReturn);
					dq(a_target + 0xB);	 // 0x6F8 - 0x6ED
				}
			}
		};

		Patch patch(isSwimmingVariable.address(), target.address());
		patch.ready();

		auto& trampoline = SKSE::GetTrampoline();
		trampoline.write_branch<6>(target.address(), L_SOS.CodeAllocation(patch, &trampoline));
		logger::info("SwimmingHook hooked {:x}", target.offset());
	}

	static void InstallWaterHook()
	{
		REL::Relocation<std::uintptr_t> ActorUpdate{ RELOCATION_ID(36357, 37348), REL::VariantOffset(0x6D3, 0x674, 0x6D3) };

		auto& trampoline = SKSE::GetTrampoline();
		_GetSubmergeLevel = trampoline.write_call<5>(ActorUpdate.address(), GetSubmergeLevel);
		logger::info("WaterHook hooked {:x}", ActorUpdate.offset());
	}

private:
	static float GetSubmergeLevel(RE::Actor* a_this, float a_zPos, RE::TESObjectCELL* a_cell)
	{
		float submergedLevel = _GetSubmergeLevel(a_this, a_zPos, a_cell);  // call to the OG

		static RE::SpellItem* WaterSlowdownSmol = NULL;
		static RE::SpellItem* WaterSlowdownBeeg = NULL;
		static RE::SpellItem* WaterSlowdownSwim = NULL;
		static RE::SpellItem* WaterSlowdownSink = NULL;
		static RE::TESDataHandler* dataHandle = NULL;

		if (!dataHandle) {	// we only need this to run once
			dataHandle = RE::TESDataHandler::GetSingleton();
			if (dataHandle) {
				WaterSlowdownSmol = dataHandle->LookupForm<RE::SpellItem>(0xD64, "SinkOrSwim.esp");
				WaterSlowdownBeeg = dataHandle->LookupForm<RE::SpellItem>(0xD65, "SinkOrSwim.esp");
				WaterSlowdownSwim = dataHandle->LookupForm<RE::SpellItem>(0xD67, "SinkOrSwim.esp");
				WaterSlowdownSink = dataHandle->LookupForm<RE::SpellItem>(0xD69, "SinkOrSwim.esp");
			}
		};

		isHeavy = FALSE;  // set to false on logic start
		if (submergedLevel >= 0.69) {
			a_this->RemoveSpell(WaterSlowdownBeeg);
			a_this->RemoveSpell(WaterSlowdownSmol);
			a_this->AddSpell(WaterSlowdownSwim);
			a_this->AddSpell(WaterSlowdownSink);

			if (ActorHasPowerArmor(a_this)) {
				if (!a_this->GetCharController()) {
					logger::info("Invalid CharController ptr, skipping gravity code.");
				} else {
					a_this->GetCharController()->gravity = 0.20f;  // set gravity so we "float" when submerged, dont let it reset
					isHeavy = TRUE;
					if (!a_this->GetActorRuntimeData().pad1EC) {  // we only need this to run ONCE when meeting the condition
						const RE::hkVector4 hkv = { -1.00f, -1.00f, -1.00f, -1.00f };
						a_this->GetCharController()->SetLinearVelocityImpl(hkv);
						a_this->GetActorRuntimeData().pad1EC = TRUE;  // this is a (presumably) unused variable that i am putting to use
						goto JustFuckingLeave;
					}
				}
			}
		} else if (submergedLevel >= 0.43) {  // everything below seems pretty self-explanatory so i dont feel the need to comment on it
			a_this->RemoveSpell(WaterSlowdownSmol);
			a_this->RemoveSpell(WaterSlowdownSwim);
			a_this->RemoveSpell(WaterSlowdownSink);
			a_this->AddSpell(WaterSlowdownBeeg);
			if (!a_this->GetCharController()) {
				logger::info("Invalid CharController ptr, skipping gravity code.");
			} else {
				a_this->GetCharController()->gravity = 1.00f;
			};
		} else if (submergedLevel >= 0.18) {
			a_this->RemoveSpell(WaterSlowdownBeeg);
			a_this->RemoveSpell(WaterSlowdownSwim);
			a_this->RemoveSpell(WaterSlowdownSink);
			a_this->AddSpell(WaterSlowdownSmol);
			if (!a_this->GetCharController()) {
				logger::info("Invalid CharController ptr, skipping gravity code.");
			} else {
				a_this->GetCharController()->gravity = 1.00f;
			};
		} else {
			a_this->RemoveSpell(WaterSlowdownSwim);
			a_this->RemoveSpell(WaterSlowdownBeeg);
			a_this->RemoveSpell(WaterSlowdownSmol);
			a_this->RemoveSpell(WaterSlowdownSink);
			if (!a_this->GetCharController()) {
				logger::info("Invalid CharController ptr, skipping gravity code.");
			} else {
				a_this->GetCharController()->gravity = 1.00f;
				a_this->GetActorRuntimeData().pad1EC = FALSE;  // set this to false when NOT meeting our condition
			};
		}
JustFuckingLeave:
		return submergedLevel;
	};

	static inline bool ActorHasPowerArmor(RE::Actor* refr)
	{
		std::vector<RE::BGSBipedObjectForm::BipedObjectSlot> bodySlots = {
			RE::BGSBipedObjectForm::BipedObjectSlot::kModChestPrimary, RE::BGSBipedObjectForm::BipedObjectSlot::kBody,
			RE::BGSBipedObjectForm::BipedObjectSlot::kModChestSecondary
		};

		std::vector<RE::BGSBipedObjectForm::BipedObjectSlot> handSlots = {
			RE::BGSBipedObjectForm::BipedObjectSlot::kHands,
			RE::BGSBipedObjectForm::BipedObjectSlot::kForearms,
			RE::BGSBipedObjectForm::BipedObjectSlot::kModArmLeft,
			RE::BGSBipedObjectForm::BipedObjectSlot::kModArmRight
		};

		std::vector<RE::BGSBipedObjectForm::BipedObjectSlot> headSlots = {
			RE::BGSBipedObjectForm::BipedObjectSlot::kCirclet,
			RE::BGSBipedObjectForm::BipedObjectSlot::kHair,
			RE::BGSBipedObjectForm::BipedObjectSlot::kLongHair,
			RE::BGSBipedObjectForm::BipedObjectSlot::kHead
		};

		std::vector<RE::BGSBipedObjectForm::BipedObjectSlot> feetSlots = {
			RE::BGSBipedObjectForm::BipedObjectSlot::kFeet,
			RE::BGSBipedObjectForm::BipedObjectSlot::kCalves,
			RE::BGSBipedObjectForm::BipedObjectSlot::kModLegLeft,
			RE::BGSBipedObjectForm::BipedObjectSlot::kModLegRight
		};

		auto hasHeavyBody = false;
		for (auto slot : bodySlots) {
			auto armor = refr->GetWornArmor(slot);
			if (armor && armor->GetArmorType() == RE::BGSBipedObjectForm::ArmorType::kHeavyArmor) {
				hasHeavyBody = true;
			}
		}

		auto hasHeavyHands = false;
		for (auto slot : handSlots) {
			auto armor = refr->GetWornArmor(slot);
			if (armor && armor->GetArmorType() == RE::BGSBipedObjectForm::ArmorType::kHeavyArmor) {
				hasHeavyHands = true;
			}
		}

		auto hasHeavyHead = false;
		for (auto slot : headSlots) {
			auto armor = refr->GetWornArmor(slot);
			if (armor && armor->GetArmorType() == RE::BGSBipedObjectForm::ArmorType::kHeavyArmor) {
				hasHeavyHead = true;
			}
		}

		auto hasHeavyFeet = false;
		for (auto slot : feetSlots) {
			auto armor = refr->GetWornArmor(slot);
			if (armor && armor->GetArmorType() == RE::BGSBipedObjectForm::ArmorType::kHeavyArmor) {
				hasHeavyFeet = true;
			}
		}
		return hasHeavyBody && hasHeavyHands && hasHeavyHead && hasHeavyFeet;
	}
	static inline REL::Relocation<decltype(GetSubmergeLevel)> _GetSubmergeLevel;
};

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(1);
	v.PluginName("SinkOrSwim");
	v.AuthorName("LokiWasTaken");
	v.UsesAddressLibrary(true);
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST_AE });
	v.UsesNoStructs(true);

	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("SinkOrSwim loaded");

	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(128);

	Loki_SinkOrSwim::InstallWaterHook();
	Loki_SinkOrSwim::InstallSwimmingHook();

	return true;
}
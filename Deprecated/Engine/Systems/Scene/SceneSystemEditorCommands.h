// Historical SceneSystem member fragment. Reference only; never compiled.

	#if 0
		// DORMANT LEGACY EXTERNAL-EDITOR PROTOCOL
		// Retained only as implementation reference. Future editor tooling is in-process
		// engine UI and must not register these message/command seams.
		void RegisterEditorCommands();
		void SendBehaviorsToEditor();
		void RegisterEntityCreateCommand(CommandSystem& cmd);
		void RegisterEntityDestroyCommand(CommandSystem& cmd);
		void RegisterEntityAddComponentCommand(CommandSystem& cmd);
		void RegisterEntityRemoveComponentCommand(CommandSystem& cmd);
		void RegisterEntitySetMaterialCommand(CommandSystem& cmd);
		void RegisterEntityBehaviorAddCommand(CommandSystem& cmd);
		void RegisterEntityBehaviorRemoveCommand(CommandSystem& cmd);
		static void AddComponentByName(Scene& scene, SerializedEntityId entityId, const std::string& componentName);
		static void RemoveComponentByName(Scene& scene, SerializedEntityId entityId, const std::string& componentName);
	#endif

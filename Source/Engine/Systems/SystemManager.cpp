#include "PCH.h"
#include "SystemManager.h"

namespace Engine
{

  int SystemManager::SmartIterate(std::function<int(Machine*)> method)
  {
    for (auto& pair : systems)
    {
      const std::string& systemName = pair.first;
      auto& machine = pair.second;

      if (!machine)
      {
        std::cerr << "Warning: Null Machine pointer for system: " << systemName << std::endl;
        continue;
      }

      // Call the passed-in method on the current machine
      int result = method(machine.get()); // Access raw pointer from unique_ptr

      // Check for errors
      if (result != 0)
      {
        std::cerr << "Error in system: " << systemName << ". Method returned: " << result << std::endl;
        return result;
      }
    }

    return 0; // Success
  }

  int SystemManager::Awake()
  {
    return SmartIterate([](Machine* machine) { return machine->Awake(); });
  }

  int SystemManager::Init()
  {
    return SmartIterate([](Machine* machine) { return machine->Init(); });
  }

  void SystemManager::Update(double dt)
  {
    SmartIterate([dt](Machine* machine) -> int
    {
      machine->Update(dt);
      return 0; // Update doesn't return anything; assume success
    });
  }

  void SystemManager::FixedUpdate(unsigned int tickThisSecond)
  {
    SmartIterate([tickThisSecond](Machine* machine) -> int
    {
      machine->FixedUpdate(tickThisSecond);
      return 0; // FixedUpdate doesn't return anything; assume success
    });
  }

  int SystemManager::Exit()
  {
    return SmartIterate([](Machine* machine) { return machine->Exit(); });
  }

}

#ifndef PokemonAutomation_PokemonLA_CobaltCoastlandsPatrol_H
#define PokemonAutomation_PokemonLA_CobaltCoastlandsPatrol_H

#include "CommonFramework/Notifications/EventNotificationsTable.h"
#include "NintendoSwitch/Framework/NintendoSwitch_SingleSwitchProgram.h"
#include "PokemonLA/Options/PokemonLA_ShinyDetectedAction.h"
#include "PokemonLA/Inference/PokemonLA_MountDetector.h"
#include "PokemonLA/Inference/PokemonLA_UnderAttackDetector.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLA{

class CobaltCoastlandsPatrol_Descriptor : public RunnableSwitchProgramDescriptor{
public:
    CobaltCoastlandsPatrol_Descriptor();
};

class CobaltCoastlandsPatrol : public SingleSwitchProgramInstance{
public:
    CobaltCoastlandsPatrol(const CobaltCoastlandsPatrol_Descriptor& descriptor);
    virtual std::unique_ptr<StatsTracker> make_stats() const override;
    virtual void program(SingleSwitchProgramEnvironment& env) override;

private:
    void run_iteration(SingleSwitchProgramEnvironment& env);

private:
    class Stats;
    class RunRoute;
    ShinyDetectedActionOption SHINY_DETECTED;
    BooleanCheckBoxOption SKIP_PATH_SHINY;

    EventNotificationOption NOTIFICATION_STATUS;
    EventNotificationOption NOTIFICATION_PROGRAM_FINISH;
    EventNotificationsOption NOTIFICATIONS;
};



}
}
}
#endif

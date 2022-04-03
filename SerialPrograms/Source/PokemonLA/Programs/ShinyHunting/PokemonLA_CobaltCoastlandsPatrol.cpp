#include "Common/Cpp/Exceptions.h"
#include "CommonFramework/Notifications/ProgramNotifications.h"
#include "CommonFramework/Tools/StatsTracking.h"

#include "CommonFramework/InferenceInfra/InferenceRoutines.h"
#include "CommonFramework/Inference/BlackScreenDetector.h"
#include "NintendoSwitch/NintendoSwitch_Settings.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "PokemonLA/PokemonLA_Settings.h"
#include "PokemonLA/Inference/Objects/PokemonLA_ButtonDetector.h"
#include "PokemonLA/Inference/PokemonLA_MapDetector.h"
#include "PokemonLA/Inference/PokemonLA_DialogDetector.h"
#include "PokemonLA/Inference/PokemonLA_OverworldDetector.h"
#include "PokemonLA/Inference/PokemonLA_ShinySoundDetector.h"
#include "PokemonLA/Inference/PokemonLA_UnderAttackDetector.h"
#include "PokemonLA/Programs/PokemonLA_MountChange.h"
#include "PokemonLA/Programs/PokemonLA_GameEntry.h"
#include "PokemonLA/Programs/PokemonLA_RegionNavigation.h"
#include "CommonFramework/Tools/ErrorDumper.h"
#include "PokemonLA_CobaltCoastlandsPatrol.h"



namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLA{



CobaltCoastlandsPatrol_Descriptor::CobaltCoastlandsPatrol_Descriptor()
    : RunnableSwitchProgramDescriptor(
        "PokemonLA:CobaltCoastlandsPatrol",
        STRING_POKEMON + " LA", "Cobalt Coastlands Patrol",
        "ComputerControl/blob/master/Wiki/Programs/PokemonLA/CobaltCoastlandsPatrol.md",
        "Do a swipe in the map looking for shinies",
        FeedbackType::REQUIRED, false,
        PABotBaseLevel::PABOTBASE_12KB
    )
{}


CobaltCoastlandsPatrol::CobaltCoastlandsPatrol(const CobaltCoastlandsPatrol_Descriptor& descriptor)
    : SingleSwitchProgramInstance(descriptor)
    , SHINY_DETECTED("0 * TICKS_PER_SECOND")
    , SKIP_PATH_SHINY("<b>Skip any Shines on the Path:</b><br>Only care about shines inside the ruins.", false)
    , NOTIFICATION_STATUS("Status Update", true, false, std::chrono::seconds(3600))
    , NOTIFICATION_PROGRAM_FINISH("Program Finished", true, true)
    , NOTIFICATIONS({
        &NOTIFICATION_STATUS,
        &SHINY_DETECTED.NOTIFICATIONS,
        &NOTIFICATION_PROGRAM_FINISH,
//        &NOTIFICATION_ERROR_RECOVERABLE,
        &NOTIFICATION_ERROR_FATAL,
    })
{
    PA_ADD_OPTION(SHINY_DETECTED);
    PA_ADD_OPTION(SKIP_PATH_SHINY);
    PA_ADD_OPTION(NOTIFICATIONS);
}


class CobaltCoastlandsPatrol::Stats : public StatsTracker, public ShinyStatIncrementer{
public:
    Stats()
        : attempts(m_stats["Attempts"])
        , errors(m_stats["Errors"])
        , shinies(m_stats["Shinies"])
    {
        m_display_order.emplace_back("Attempts");
        m_display_order.emplace_back("Errors", true);
        m_display_order.emplace_back("Shinies", true);
    }
    virtual void add_shiny() override{
        shinies++;
    }

    std::atomic<uint64_t>& attempts;
    std::atomic<uint64_t>& errors;
    std::atomic<uint64_t>& shinies;
};

std::unique_ptr<StatsTracker> CobaltCoastlandsPatrol::make_stats() const{
    return std::unique_ptr<StatsTracker>(new Stats());
}

void goto_any_camp_from_overworld(ProgramEnvironment& env, ConsoleHandle& console, const TravelLocation& location){
    //  Open the map.
    pbf_press_button(console, BUTTON_MINUS, 20, 30);
    {
        MapDetector detector;
        int ret = wait_until(
            env, console,
            std::chrono::seconds(5),
            { &detector }
        );
        if (ret < 0){
            dump_image(env.logger(), env.program_info(), "MapNotFound", console.video().snapshot());
            throw OperationFailedException(console, "Map not detected after 5 seconds.");
        }
        console.log("Found map!");
        env.wait_for(std::chrono::milliseconds(500));
    }

    //  Warp to sub-camp.
    pbf_press_button(console, BUTTON_X, 20, 30);
    {
        ButtonDetector detector(
            console, console,
            ButtonType::ButtonA,
            {0.55, 0.40, 0.20, 0.40},
            std::chrono::milliseconds(200), true
        );
        int ret = wait_until(
            env, console,
            std::chrono::seconds(2),
            { &detector }
        );
        if (ret < 0){
            throw OperationFailedException(console, "Unable to fly. Are you under attack?");
        }
    }

    pbf_wait(console, 50);

    if (location.warp_slot != 0){
        for (size_t c = 0; c < location.warp_slot; c++){
            pbf_press_dpad(console, DPAD_DOWN, 20, 30);
        }
    }

    for (size_t c = 0; c < location.warp_sub_slot; c++){
        pbf_press_dpad(console, DPAD_DOWN, 20, 30);
    }

    pbf_mash_button(console, BUTTON_A, 125);

    BlackScreenOverWatcher black_screen(COLOR_RED, {0.1, 0.1, 0.8, 0.6});
    int ret = wait_until(
        env, console,
        std::chrono::seconds(20),
        { &black_screen }
    );
    if (ret < 0){
        dump_image(env.logger(), env.program_info(), "FlyToCamp", console.video().snapshot());
        throw OperationFailedException(console, "Failed to fly to camp after 20 seconds.");
    }
    console.log("Arrived at sub-camp...");
    env.wait_for(std::chrono::milliseconds((uint64_t)(GameSettings::instance().POST_WARP_DELAY * 1000)));

    if (location.post_arrival_maneuver == nullptr){
        return;
    }

    location.post_arrival_maneuver(console);
    console.botbase().wait_for_all_requests();
}

void tilt_and_align(const BotBaseContext& context, uint8_t tiltLeft, uint8_t tiltRight){
    pbf_wait(context, (uint16_t)(0.5 * TICKS_PER_SECOND));

    if(tiltLeft != 0)
        pbf_move_right_joystick(context, 0, 127, tiltLeft, 20);  //left

    if(tiltRight != 0)
        pbf_move_right_joystick(context, 255, 127, tiltRight, 20); //right

    pbf_wait(context, (uint16_t)(0.5 * TICKS_PER_SECOND));
    pbf_move_left_joystick(context, 127, 0, 20, 20);
    pbf_wait(context, (uint16_t)(0.5 * TICKS_PER_SECOND));
}

void reset_height(const BotBaseContext& context, double waitTime){
    pbf_press_button(context, BUTTON_PLUS, 20, 20);
    pbf_wait(context, (uint16_t)(waitTime * TICKS_PER_SECOND));
    pbf_press_button(context, BUTTON_PLUS, 20, 20);
}

void CobaltCoastlandsPatrol::run_iteration(SingleSwitchProgramEnvironment& env){
    Stats& stats = env.stats<Stats>();

    stats.attempts++;

    env.console.log("Beginning Shiny Detection...");
    {
        ShinySoundDetector shiny_detector(env.console, SHINY_DETECTED.stop_on_shiny());

        change_mount(env.console, MountState::BRAVIARY_OFF);

        //ROUTE 3 - Walrein & Ambipom
        goto_any_camp_from_overworld(env, env.console, TravelLocations::instance().Coastlands_Beachside);

        run_until(env, env.console,
            [](const BotBaseContext& context){
                tilt_and_align(context, 0, 105);
                pbf_press_button(context, BUTTON_PLUS, 20, 20);
                pbf_wait(context, (uint16_t)(2 * TICKS_PER_SECOND));
                reset_height(context, 1);
                pbf_press_button(context, BUTTON_B, (uint16_t)(6 * TICKS_PER_SECOND), 20);
                pbf_wait(context, (uint16_t)(1 * TICKS_PER_SECOND));

                tilt_and_align(context, 0, 52);
                pbf_press_button(context, BUTTON_B, (uint16_t)(10 * TICKS_PER_SECOND), 20);
                reset_height(context, 1);
                pbf_press_button(context, BUTTON_B, (uint16_t)(10 * TICKS_PER_SECOND), 20);
                reset_height(context, 1);
                pbf_press_button(context, BUTTON_B, (uint16_t)(4 * TICKS_PER_SECOND), 10);
                pbf_wait(context, (uint16_t)(1 * TICKS_PER_SECOND));

                tilt_and_align(context, 40, 0);
                pbf_press_button(context, BUTTON_B, (uint16_t)(10 * TICKS_PER_SECOND), 20);

            },
            { &shiny_detector }
        );

        if (shiny_detector.detected()){
           stats.shinies++;
           on_shiny_sound(env, env.console, SHINY_DETECTED, shiny_detector.results());
        }
    };

    env.console.log("No shiny detected, returning to Jubilife!");
    goto_camp_from_overworld(env, env.console, SHINY_DETECTED, stats);
}


void CobaltCoastlandsPatrol::program(SingleSwitchProgramEnvironment& env){
    Stats& stats = env.stats<Stats>();

    //  Connect the controller.
    pbf_press_button(env.console, BUTTON_LCLICK, 5, 5);

    //while (true){
        env.update_stats();
        send_program_status_notification(
            env.logger(), NOTIFICATION_STATUS,
            env.program_info(),
            "",
            stats.to_str()
        );
        try{
            run_iteration(env);
        }catch (OperationFailedException&){
            stats.errors++;
            pbf_press_button(env.console, BUTTON_HOME, 20, GameSettings::instance().GAME_TO_HOME_DELAY);
            reset_game_from_home(env, env.console, ConsoleSettings::instance().TOLERATE_SYSTEM_UPDATE_MENU_FAST);
        }catch (OperationCancelledException&){
            //break;
        }
    //}

    env.update_stats();
    send_program_finished_notification(
        env.logger(), NOTIFICATION_PROGRAM_FINISH,
        env.program_info(),
        "",
        stats.to_str()
    );
}

}
}
}

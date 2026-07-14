#pragma once

#include <limits>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace engine {

// A per-entity animation state machine with automatic cross-fading. Each state
// names a clip (index into a SkinnedModel's animations) with loop/speed settings
// and an optional activation range on a single float PARAMETER (e.g. locomotion
// speed): the controller auto-selects the state whose range contains the current
// parameter and cross-fades into it. States with the default (infinite) range are
// driven manually via Play().
//
// It is deliberately model-agnostic -- it only tracks indices and times -- so it
// is fully unit-testable without any GL/model. A poser (UpdateAnimations) reads
// CurrentClip()/PrevClip()/times/Blend() to produce the actual pose.
class AnimationController {
public:
    struct State {
        std::string name;
        int   clip  = 0;         // index into SkinnedModel::Animations()
        bool  loop  = true;
        float speed = 1.0f;      // playback-rate multiplier
        // Auto-selected while param is in [paramMin, paramMax]. Default = manual.
        float paramMin = -std::numeric_limits<float>::infinity();
        float paramMax =  std::numeric_limits<float>::infinity();
        float durationSeconds = 0.0f;
    };

    struct Transition {
        enum class Compare {
            GreaterOrEqual = 0,
            Less = 1,
            Equal = 2,
            NotEqual = 3
        };

        int from = -1;
        int to = -1;
        std::string parameter = "Speed";
        Compare compare = Compare::GreaterOrEqual;
        float threshold = 0.0f;
        float fade = 0.2f;
        float exitTime = 0.0f; // normalized current-state time, 0 = no wait
        int priority = 0;      // higher values win when several transitions pass
        bool canInterrupt = false;
    };

    float crossfade = 0.25f;     // seconds to blend between states

    // Add a state; the first one added becomes current. Returns its index.
    int AddState(const State& s);
    void AddTransition(const Transition& t);

    // Locomotion helper: idle / walk / run auto-selected by a speed parameter.
    static AnimationController Locomotion(int idleClip, int walkClip, int runClip, float walkAt = 0.15f,
                                          float runAt = 3.0f, float crossfadeSeconds = 0.2f);
                        
    void SetParameter(float v) { SetParameter("Speed", v); }
    void SetParameter(const std::string& name, float value);
    void SetBoolParameter(const std::string& name, bool value);
    void SetTriggerParameter(const std::string& name);
    void ResetTriggerParameter(const std::string& name);
    float Parameter() const { return m_param; }
    float Parameter(const std::string& name, float fallback = 0.0f) const;
    bool BoolParameter(const std::string& name, bool fallback = false) const;
    const std::unordered_map<std::string, float>& Parameters() const { return m_parameters; }

    // Manually transition to a state (cross-fades unless immediate).
    void Play(int stateIndex, bool immediate = false);

    // Advance time, evaluate parameter-driven transitions, and progress the blend.
    void Update(float dt);
    
    // --- Poser inputs ----------------------------------------------------
    bool  Blending()    const { return m_prev >= 0 && m_blend < 1.0f; }
    float Blend()       const { return m_blend; }     // 0 = prev, 1 = current
    int   CurrentState()const { return m_cur; }
    int   CurrentClip() const { return (m_cur  >= 0) ? m_states[static_cast<std::size_t>(m_cur)].clip  : -1; }
    int   PrevClip()    const { return (m_prev >= 0) ? m_states[static_cast<std::size_t>(m_prev)].clip : -1; }
    bool  CurrentLoop() const { return (m_cur  >= 0) ? m_states[static_cast<std::size_t>(m_cur)].loop  : true; }
    float CurrentTime() const { return m_curTime; }
    float PrevTime()    const { return m_prevTime; }
    const std::string& CurrentStateName() const;
    std::size_t StateCount() const { return m_states.size(); }
    std::size_t TransitionCount() const { return m_transitions.size(); }

private:
    int  PickByParam() const;
    int  PickByTransition() const;
    bool TestTransition(const Transition& transition) const;
    bool ExitTimeReached(const Transition& transition) const;
    const Transition* BestTransition() const;
    void Begin(int to, bool immediate);
    void Begin(int to, bool immediate, float fadeSeconds);

    std::vector<State> m_states;
    std::vector<Transition> m_transitions;
    int   m_cur = -1, m_prev = -1;
    float m_curTime = 0.0f, m_prevTime = 0.0f;
    float m_blend = 1.0f;   // 1 = fully in current
    float m_param = 0.0f;
    std::unordered_map<std::string, float> m_parameters;
    std::unordered_set<std::string> m_triggers;
};

} // namespace engine
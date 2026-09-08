#pragma once

// Engine loading screen -- progress model (thread-safe, renderer-agnostic).
//
// Aggregates the loading work of every engine subsystem that takes measurable time (scene parse,
// meshes, textures, materials, skinned models, animation controllers, collision meshes, physics
// cook, audio, scripts, fonts) into a single weighted 0..1 fraction + a "currently loading X" label,
// so a loading screen can draw one honest progress bar. Loading typically runs on a worker thread
// while the render thread draws the bar, so reads of Fraction()/Percent() are lock-free (a cached
// atomic) and mutations take a short lock. This header holds NO GL -- the actual bar is drawn by the
// UI layer from LoadingBarGeometry; that keeps the model unit-testable.

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace engine {

// The subsystems a full scene/project load touches. Used to build the standard task set with
// sensible relative weights; games can add their own tasks too.
enum class LoadStage {
    Project,          // project/settings/registry
    Scene,            // parse the scene/level file
    Meshes,           // static models
    Textures,         // texture assets
    Materials,        // material resolve
    SkinnedModels,    // skeletal meshes
    Animation,        // animation controllers / clips
    CollisionMeshes,  // physics collision geometry cook
    Physics,          // physics world build (static cache, broadphase)
    Audio,            // audio banks / cues
    Scripts,          // script module load + OnCreate
    Fonts,            // imported .3dgfont atlases
    Finalize          // render scene extraction, first frame prep
};

inline const char* LoadStageName(LoadStage s) {
    switch (s) {
        case LoadStage::Project: return "Project";
        case LoadStage::Scene: return "Loading scene";
        case LoadStage::Meshes: return "Loading meshes";
        case LoadStage::Textures: return "Loading textures";
        case LoadStage::Materials: return "Resolving materials";
        case LoadStage::SkinnedModels: return "Loading skeletal meshes";
        case LoadStage::Animation: return "Building animations";
        case LoadStage::CollisionMeshes: return "Cooking collision";
        case LoadStage::Physics: return "Building physics";
        case LoadStage::Audio: return "Loading audio";
        case LoadStage::Scripts: return "Loading scripts";
        case LoadStage::Fonts: return "Loading fonts";
        case LoadStage::Finalize: return "Finalizing";
    }
    return "Loading";
}

struct LoadTask {
    std::string   label;
    float         weight   = 1.0f;   // relative time estimate
    float         progress = 0.0f;   // 0..1
    bool Done() const { return progress >= 1.0f; }
};

class LoadingProgress {
public:
    // Register a task; returns its id. Weight is a relative time estimate (heavier = more of the bar).
    int Add(std::string label, float weight = 1.0f) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.push_back({std::move(label), weight > 0.0f ? weight : 0.0001f, 0.0f});
        RecomputeLocked();
        return static_cast<int>(m_tasks.size()) - 1;
    }
    int Add(LoadStage stage, float weight = 1.0f) { return Add(LoadStageName(stage), weight); }

    void SetLabel(int id, std::string label) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (id >= 0 && id < static_cast<int>(m_tasks.size())) m_tasks[id].label = std::move(label);
    }
    void SetProgress(int id, float fraction) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (id < 0 || id >= static_cast<int>(m_tasks.size())) return;
        m_tasks[id].progress = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
        RecomputeLocked();
    }
    // Progress from a done/total count (e.g. entities resolved so far).
    void Step(int id, int done, int total) { SetProgress(id, total > 0 ? static_cast<float>(done) / total : 1.0f); }
    void Complete(int id) { SetProgress(id, 1.0f); }
    void CompleteAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (LoadTask& t : m_tasks) t.progress = 1.0f;
        RecomputeLocked();
    }

    // Weighted overall progress in [0,1]; lock-free read (safe from the render thread).
    float Fraction() const { return m_fraction.load(std::memory_order_relaxed); }
    int   Percent()  const { return static_cast<int>(Fraction() * 100.0f + 0.5f); }
    bool  Done()     const { return Fraction() >= 1.0f; }

    // The first still-loading task's label (what to show under the bar). Empty when done.
    std::string CurrentLabel() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const LoadTask& t : m_tasks) if (!t.Done()) return t.label;
        return {};
    }
    std::size_t TaskCount() const { std::lock_guard<std::mutex> lock(m_mutex); return m_tasks.size(); }
    std::vector<LoadTask> Snapshot() const { std::lock_guard<std::mutex> lock(m_mutex); return m_tasks; }
    void Reset() { std::lock_guard<std::mutex> lock(m_mutex); m_tasks.clear(); m_fraction.store(0.0f); }

private:
    void RecomputeLocked() {
        float totalW = 0.0f, doneW = 0.0f;
        for (const LoadTask& t : m_tasks) { totalW += t.weight; doneW += t.weight * t.progress; }
        m_fraction.store(totalW > 0.0f ? doneW / totalW : 0.0f, std::memory_order_relaxed);
    }
    mutable std::mutex m_mutex;
    std::vector<LoadTask> m_tasks;
    std::atomic<float> m_fraction{0.0f};
};

// Ids for the standard engine loading tasks, returned so the loader can drive each stage.
struct EngineLoadTasks {
    int project = -1, scene = -1, meshes = -1, textures = -1, materials = -1, skinned = -1,
        animation = -1, collision = -1, physics = -1, audio = -1, scripts = -1, fonts = -1, finalize = -1;
};

// Populate `progress` with the standard task set covering every subsystem that loads, with relative
// weights tuned to typical cost (asset I/O dominates). Returns the task ids so the loader updates each
// stage as it runs (e.g. tasks.Step for per-entity asset resolve inside ResolveRegistryAssets).
inline EngineLoadTasks BuildEngineLoadingProgress(LoadingProgress& progress) {
    EngineLoadTasks t;
    t.project   = progress.Add(LoadStage::Project,         0.5f);
    t.scene     = progress.Add(LoadStage::Scene,           1.0f);
    t.meshes    = progress.Add(LoadStage::Meshes,          3.0f);
    t.textures  = progress.Add(LoadStage::Textures,        3.0f);
    t.materials = progress.Add(LoadStage::Materials,       1.0f);
    t.skinned   = progress.Add(LoadStage::SkinnedModels,   2.0f);
    t.animation = progress.Add(LoadStage::Animation,       1.5f);
    t.collision = progress.Add(LoadStage::CollisionMeshes, 2.0f);
    t.physics   = progress.Add(LoadStage::Physics,         1.0f);
    t.audio     = progress.Add(LoadStage::Audio,           1.0f);
    t.scripts   = progress.Add(LoadStage::Scripts,         1.0f);
    t.fonts     = progress.Add(LoadStage::Fonts,           0.5f);
    t.finalize  = progress.Add(LoadStage::Finalize,        0.5f);
    return t;
}

// Pure geometry for the loading bar so the UI layer just draws two rectangles (track + fill). All in
// pixels; fillWidth is clamped to the track. Unit-testable; no GL.
struct LoadingBarGeometry {
    float trackX = 0, trackY = 0, trackW = 0, trackH = 0;   // full bar background
    float fillX  = 0, fillY  = 0, fillW  = 0, fillH  = 0;   // filled portion (progress)
};

inline LoadingBarGeometry ComputeLoadingBar(float centerX, float centerY, float width, float height,
                                            float fraction) {
    if (fraction < 0.0f) fraction = 0.0f; else if (fraction > 1.0f) fraction = 1.0f;
    LoadingBarGeometry g;
    g.trackX = centerX - width * 0.5f;
    g.trackY = centerY - height * 0.5f;
    g.trackW = width; g.trackH = height;
    g.fillX = g.trackX; g.fillY = g.trackY;
    g.fillW = width * fraction; g.fillH = height;
    return g;
}

} // namespace engine

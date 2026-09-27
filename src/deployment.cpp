#include "deployment.h"

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void apply_patch(const fs::path& working_dir,
                        const std::string& schema_id,
                        const YAML::Node& patch);

std::unique_ptr<Rime>
prepare_deployment(const fs::path& source_dir,
                   const std::string& schema_id,
                   const std::optional<std::string>& cache_dir,
                   const YAML::Node& deployment)
{
    auto rime = std::make_unique<Rime>(source_dir, schema_id, cache_dir,
                                      /* deploy_now */ false);
    if (deployment["patch"]) {
        apply_patch(rime->get_working_dir(), schema_id, deployment["patch"]);
    }
    rime->deploy();
    return rime;
}

std::unique_ptr<Session>
prepare_session(Rime& rime,
                const std::string& schema_id,
                const YAML::Node& options)
{
    auto session = rime.create_session();
    if (!session) {
        std::cerr << "cannot create Rime session\n";
        return nullptr;
    }
    if (!session->select_schema(schema_id)) {
        std::cerr << "cannot select schema '" << schema_id << "'\n";
        return nullptr;
    }
    if (options) {
        for (const auto& option : options) {
            session->set_option(option.first.as<std::string>(),
                                option.second.as<bool>());
        }
    }
    return session;
}

static void
apply_patch(const fs::path& working_dir,
            const std::string& schema_id,
            const YAML::Node& patch)
{
    std::ofstream fout(working_dir / "data" / (schema_id + ".custom.yaml"));
    YAML::Node document;
    document["patch"] = patch;
    fout << document;
}

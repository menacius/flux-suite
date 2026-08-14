#include "title-snapshot.h"

#include <cassert>
#include <iostream>
#include <memory>

int main()
{
    Title title;
    title.id = "snapshot-title";
    auto layer = std::make_shared<Layer>();
    layer->id = "layer";
    layer->name = "original";
    title.layers.push_back(layer);

    Title snapshot = clone_title_snapshot(title);
    assert(snapshot.id == title.id);
    assert(snapshot.layers.size() == 1);
    assert(snapshot.layers[0]);
    assert(snapshot.layers[0].get() != title.layers[0].get());

    snapshot.layers[0]->name = "snapshot";
    assert(title.layers[0]->name == "original");

    auto shared_title = std::make_shared<Title>(title);
    auto shared_snapshot = clone_title_snapshot(shared_title);
    assert(shared_snapshot);
    assert(shared_snapshot.get() != shared_title.get());
    assert(shared_snapshot->layers[0].get() != shared_title->layers[0].get());

    /* Undo/redo must restore all authored title state, including cameras and
     * 3D layer properties, while preserving runtime source/cue/proxy identity. */
    Title live;
    live.id = "live-title-id";
    live.name = "Before";
    live.render_camera_override_id = "editor-orbit-camera";
    live.proxy_metadata.proxy_path = "live-machine-proxy.ogsf";
    live.proxy_metadata.complete = true;
    live.current_cue_row = 3;
    live.pending_cue_row = 4;
    live.cue_uncue_requested = true;
    live.cue_revision = 41;
    live.playlist_active = true;
    live.playlist_next_row = 5;
    live.playlist_next_due_ms = 123456;
    live.playlist_stop_after_due = true;
    live.cue_background_persistence = true;
    live.cue_text_persistence = true;
    live.cue_persistence_transition = true;
    live.cue_persistent_text_columns = {true, false, true};

    Title authored;
    authored.id = "historical-snapshot-id";
    authored.name = "After Undo";
    authored.cameras.clear();
    TitleCamera camera;
    camera.id = "camera-authored";
    camera.name = "Authored Camera";
    camera.use_canvas_default = false;
    camera.position_3d_path_enabled = true;
    camera.position_3d.static_value = {10.0, 20.0, -750.0};
    camera.orientation_y.static_value = 35.0;
    camera.projection = CameraProjection::Orthographic;
    camera.projection_mode.static_value = 1.0;
    authored.cameras.push_back(camera);
    authored.active_camera.static_value = camera.id;
    authored.active_camera_id = camera.id;

    auto authored_layer = std::make_shared<Layer>();
    authored_layer->id = "3d-layer";
    authored_layer->name = "Restored 3D Layer";
    authored_layer->position_3d_path_enabled = true;
    authored_layer->position_3d.static_value = {100.0, 200.0, 300.0};
    authored_layer->rotation_x.static_value = 15.0;
    authored_layer->rotation_y.static_value = 25.0;
    authored_layer->orientation_z.static_value = 45.0;
    authored_layer->scale_z.static_value = 2.0;
    authored_layer->anchor_z.static_value = 12.0;
    authored_layer->camera_id = camera.id;
    authored_layer->camera_assignment.static_value = camera.id;
    authored.layers.push_back(authored_layer);
    authored.live_text_rows = {{"cue 0"}, {"cue 1"}, {"cue 2"}, {"cue 3"}, {"cue 4"}, {"cue 5"}};
    authored.live_text_column_order = {"3d-layer", "second", "third"};

    restore_title_authoring_snapshot(live, authored);
    assert(live.id == "live-title-id");
    assert(live.name == "After Undo");
    assert(live.cameras.size() == 1);
    assert(live.cameras[0].id == "camera-authored");
    assert(live.cameras[0].position_3d_path_enabled);
    assert(live.cameras[0].orientation_y.static_value == 35.0);
    assert(live.active_camera.static_value == "camera-authored");
    assert(live.layers.size() == 1);
    assert(live.layers[0]);
    assert(live.layers[0].get() != authored.layers[0].get());
    assert(live.layers[0]->position_3d_path_enabled);
    assert(live.layers[0]->position_3d.static_value.z == 300.0);
    assert(live.layers[0]->rotation_x.static_value == 15.0);
    assert(live.layers[0]->camera_assignment.static_value == "camera-authored");
    assert(live.live_text_rows.size() == 6);

    assert(live.render_camera_override_id == "editor-orbit-camera");
    assert(live.proxy_metadata.proxy_path == "live-machine-proxy.ogsf");
    assert(live.proxy_metadata.complete);
    assert(live.current_cue_row == 3);
    assert(live.pending_cue_row == 4);
    assert(live.cue_uncue_requested);
    assert(live.cue_revision == 42);
    assert(live.playlist_active);
    assert(live.playlist_next_row == 5);
    assert(live.playlist_next_due_ms == 123456);
    assert(live.playlist_stop_after_due);
    assert(live.cue_background_persistence);
    assert(live.cue_text_persistence);
    assert(live.cue_persistence_transition);
    assert(live.cue_persistent_text_columns.size() == 3);

    /* Repeated restore must release the previous deep layer graph rather than
     * retaining one graph per undo/open-close cycle. */
    for (int iteration = 0; iteration < 1000; ++iteration) {
        std::weak_ptr<Layer> previous_layer = live.layers.front();
        restore_title_authoring_snapshot(live, authored);
        assert(previous_layer.expired());
        assert(live.layers.front().get() != authored.layers.front().get());
    }

    std::cout << "deep title snapshot and complete undo restoration contract passed\n";
    return 0;
}

#include "live-text-cue-utils.h"
#include "live-text-cue-style-prune-test-support.h"

#include <cassert>
#include <iostream>
#include <memory>

/* The production implementation also generates UUID-backed row IDs. This
 * standalone contract test needs only the parallel-size invariant. */
void ensure_live_text_row_ids(Title &title)
{
    title.live_text_row_ids.resize(title.live_text_rows.size());
}

int main()
{
    auto title = std::make_shared<Title>();

    auto text = std::make_shared<Layer>();
    text->id = "text";
    text->type = LayerType::Text;
    text->expose_text = true;
    text->text_content = "Text default";

    auto image = std::make_shared<Layer>();
    image->id = "image";
    image->type = LayerType::Image;
    image->expose_text = true;
    image->image_path = "image-default.png";

    auto hidden = std::make_shared<Layer>();
    hidden->id = "hidden";
    hidden->type = LayerType::Text;
    hidden->expose_text = false;

    title->layers = {text, image, hidden};
    title->live_text_column_order = {"image", "text"};
    title->live_text_rows = {{"old-image.png", "old-text"}};

    auto exposed = fxm::live_text::exposed_text_layers(title);
    assert(exposed.size() == 2);
    assert(exposed[0] == image);
    assert(exposed[1] == text);

    /* Simulate an editor reorder. Values must follow stable layer IDs, not
     * their former column indices. */
    title->live_text_column_order = {"image", "text"};
    std::vector<std::shared_ptr<Layer>> reordered = {text, image};
    fxm::live_text::normalize_live_text_rows(title, reordered);
    assert(title->live_text_column_order ==
           std::vector<std::string>({"text", "image"}));
    assert(title->live_text_rows[0][0] == "old-text");
    assert(title->live_text_rows[0][1] == "old-image.png");

    image->exposed_single_value = true;
    title->live_text_rows.push_back({"second-text", "other-image.png"});
    fxm::live_text::normalize_live_text_rows(title, reordered);
    assert(title->live_text_rows[1][1] == title->live_text_rows[0][1]);
    assert(fxm::live_text::live_cue_layer_value(image) == "image-default.png");

    std::cout << "shared live-text cue ordering and row normalization passed\n";
    return 0;
}

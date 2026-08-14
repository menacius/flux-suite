#pragma once

#include "title-data.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QVector3D>
#include <QVector4D>

#include <cstddef>
#include <string>
#include <vector>

namespace fxm::transform3d {

/* Shared planar-3D evaluation for editor and OBS output.
 *
 * Coordinate system
 *   +X: canvas right
 *   +Y: canvas down
 *   +Z: away from the active camera
 *
 * Local transform order (right-most operation is applied first):
 *   parent * T(position) * Orientation * Rz * Ry * Rx * Scale * T(-anchorZ)
 * XY anchor/origin remains the existing normalized origin_prop and is applied
 * by the raster/presentation code exactly as it was in the 2D pipeline.
 */
struct CameraRenderState {
    QMatrix4x4 view;
    QMatrix4x4 projection_matrix;
    CameraProjection projection = CameraProjection::Perspective;
    float vertical_fov = 50.0f;
    float aspect = 1.0f;
    float near_clip = 0.1f;
    float far_clip = 10000.0f;
    float ortho_half_width = 1.0f;
    float ortho_half_height = 1.0f;
};

struct EvaluatedTransform {
    QMatrix4x4 world;
    QTransform projected;
    QVector3D camera_position;
    QVector3D world_normal {0.0f, 0.0f, -1.0f};
    double camera_depth = 0.0;
    double projected_winding = 0.0;
    bool projectable = false;
    bool front_facing = true;
    bool inherited_3d = false;
};

bool layer_supports_3d(const Layer &layer);
bool layer_or_ancestor_uses_3d(const Title &title, const Layer &layer);
bool title_camera_has_animation(const Title &title);
bool layer_passes_backface_culling(const Title &title, const Layer &layer,
                                   double title_time);
/* Broader planar-surface compatibility shared by hardware and ordered
 * fallback compositing. It permits projected track mattes and padded
 * layer-space effects while excluding post-transform/screen-space effects that
 * require temporal samples or destination access. */
bool depth_compositor_surface_candidate(const Title &title, const Layer &layer,
                                        double title_time);
bool hardware_depth_candidate(const Title &title, const Layer &layer,
                              double title_time);
bool hardware_depth_writer(const Title &title, const Layer &layer,
                           double title_time);
bool hardware_depth_read_only(const Title &title, const Layer &layer,
                              double title_time);
/* True for simple normal-blend 3D planes whose authored or transition opacity
 * is below one. They render after the opaque depth pass, far-to-near, with
 * authored layer order as the deterministic tie-break. */
bool hardware_depth_transparent_candidate(const Title &title,
                                          const Layer &layer,
                                          double title_time);

/* Conservative painter-order fallback for destination-aware compositing.
 * Compatible automatic-depth 3D surfaces are sorted far-to-near in camera/
 * world space across the complete sibling scene. Layer-list placement is only
 * a deterministic equal-depth tie-break. Motion-blurred planes retain their
 * camera-depth order; screen-space, container and explicit Layer Order
 * surfaces retain authored order. */
std::vector<std::size_t> ordered_root_layer_indices(
    const Title &title, std::size_t first, std::size_t last, double title_time);
std::vector<const Layer *> ordered_group_children(
    const Title &title, const std::string &group_id, double title_time);
/* Camera animation is resolved without changing the legacy static-camera
 * path. When no switch/assignment/projection keys exist these helpers return
 * the exact compatibility mirrors used by Development Version 206. */
std::string resolved_active_camera_id(const Title &title, double title_time);
std::string resolved_layer_camera_id(const Title &title, const Layer &layer,
                                     double title_time);
CameraProjection evaluated_camera_projection(const TitleCamera &camera,
                                              double title_time);
const TitleCamera *active_camera(const Title &title,
                                 const std::string &camera_id = {},
                                 double title_time = 0.0);
bool camera_render_state(const Title &title, double title_time,
                         CameraRenderState &state,
                         const std::string &camera_id = {});
EvaluatedTransform evaluate(const Title &title, const Layer &layer,
                            double title_time);

/* Editor-facing world-space helpers. They share the exact renderer transform
 * and camera implementation, preventing gizmos, hit testing, and OBS output
 * from drifting apart. */
QMatrix4x4 layer_world_matrix(const Title &title, const Layer &layer,
                              double title_time);
QMatrix4x4 layer_parent_world_matrix(const Title &title, const Layer &layer,
                                     double title_time);
bool project_world_point(const Title &title, const QVector3D &world_point,
                         double title_time, QPointF &canvas_point,
                         double *camera_depth = nullptr,
                         const std::string &camera_id = {});
bool canvas_world_ray(const Title &title, const QPointF &canvas_point,
                      double title_time, QVector3D &ray_origin,
                      QVector3D &ray_direction,
                      const std::string &camera_id = {});
bool canvas_point_on_world_plane(const Title &title, const QPointF &canvas_point,
                                 double title_time,
                                 const QVector3D &plane_point,
                                 const QVector3D &plane_normal,
                                 QVector3D &world_point,
                                 const std::string &camera_id = {});
/* Produces the exact homogeneous clip-space position used by the hardware
 * depth compositor. local_point is expressed in the layer's pre-transform
 * plane, after raster/effect bounds have been mapped to logical units. */
bool layer_local_clip_point(const Title &title, const Layer &layer,
                            double title_time, const QVector3D &local_point,
                            QVector4D &clip_point,
                            const std::string &camera_id = {});
/* Build a projective transform from an arbitrary source quad directly to
 * the exact canvas projection of the matching layer-local quad. Unlike the
 * generic unit-square transform returned by evaluate(), this is conditioned
 * on the real raster rectangle and therefore remains stable when a normalized
 * XY anchor places the artwork far from local (0, 0). */
bool projected_local_quad_transform(
    const Title &title, const Layer &layer, double title_time,
    const QPolygonF &source_quad, const QPolygonF &local_quad,
    QTransform &source_to_canvas,
    const std::string &camera_id = {});
/* Project a transform-neutral padded raster/effect rectangle through the exact
 * camera/world matrices used by the renderer. The rectangle is clipped in
 * homogeneous clip space before conversion to canvas pixels, so off-canvas or
 * near-plane-crossing effects and editor overlays remain finite/stable. */
bool projected_local_polygon(const Title &title, const Layer &layer,
                             double title_time,
                             const QPolygonF &local_polygon,
                             QRectF &canvas_bounds,
                             QPolygonF *clipped_polygon = nullptr,
                             const std::string &camera_id = {});
bool projected_local_bounds(const Title &title, const Layer &layer,
                            double title_time, const QRectF &local_bounds,
                            QRectF &canvas_bounds,
                            QPolygonF *clipped_polygon = nullptr,
                            const std::string &camera_id = {});
QVector3D world_delta_to_parent_space(const Title &title, const Layer &layer,
                                      double title_time,
                                      const QVector3D &world_delta);

/* Returns legacy_transform unchanged for a pure 2D chain. This is the central
 * compatibility guard used by both renderer surfaces. */
QTransform projected_or_legacy(const Title &title, const Layer &layer,
                               double title_time,
                               const QTransform &legacy_transform,
                               EvaluatedTransform *details = nullptr);

} // namespace fxm::transform3d

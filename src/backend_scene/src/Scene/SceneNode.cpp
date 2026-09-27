#include "SceneNode.h"

#include <Eigen/Geometry>

using namespace wallpaper;
using namespace Eigen;

Matrix4d SceneNode::GetLocalTrans() const {
    Affine3d trans = Affine3d::Identity();
    trans.prescale(m_scale.cast<double>());

    trans.prerotate(AngleAxis<double>(m_rotation.x(), Vector3d::UnitX())); // x
    trans.prerotate(AngleAxis<double>(m_rotation.y(), Vector3d::UnitY())); // y
    trans.prerotate(AngleAxis<double>(m_rotation.z(), Vector3d::UnitZ())); // z

    trans.pretranslate(m_translate.cast<double>());

    return trans.matrix();
}

void SceneNode::UpdateTrans() const {
    if (m_parent) m_parent->UpdateTrans();
    const u64 parent_version = m_parent ? m_parent->m_version : 0;
    if (! m_dirty && parent_version == m_parent_version) return;
    m_dirty          = false;
    m_parent_version = parent_version;
    {
        Affine3d trans = Affine3d::Identity();
        if (m_parent) {
            trans *= m_parent->m_trans;
        }
        m_trans = (trans * GetLocalTrans()).matrix();
    }
    m_version++;
}

void SceneNode::MarkTransDirty() {
    if (! m_dirty) {
        m_dirty = true;
        for (auto& child : m_children) {
            child->MarkTransDirty();
        }
    }
}

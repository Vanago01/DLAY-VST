#include "MotionCurves.h"

namespace dlay
{

const juce::Identifier MotionCurves::motionType { "MOTION" };
const juce::Identifier MotionCurves::laneType   { "LANE" };
const juce::Identifier MotionCurves::pointType  { "PT" };
const juce::Identifier MotionCurves::propX      { "x" };
const juce::Identifier MotionCurves::propY      { "y" };
const juce::Identifier MotionCurves::propCurve  { "c" };
const juce::Identifier MotionCurves::propLane   { "lane" };

namespace
{
    /** A point as it may be used: inside its range, and a number. Sessions and preset files
        are text, and "nan" parses as a float - it would pass every range check. */
    float usable (float v, float lo, float hi, float fallback) noexcept
    {
        return std::isfinite (v) ? juce::jlimit (lo, hi, v) : fallback;
    }

    CurvePoint usable (const CurvePoint& p) noexcept
    {
        return { usable (p.x, 0.0f, 1.0f, 0.0f), usable (p.y, 0.0f, 1.0f, 0.5f), usable (p.curve, -1.0f, 1.0f, 0.0f) };
    }

    /** No more points than a lane holds: the first, the last, and the others evenly between. */
    void thinOut (std::vector<CurvePoint>& points)
    {
        const size_t n = points.size(), keep = (size_t) MotionCurves::kMaxPoints;

        if (n <= keep)
            return;

        std::vector<CurvePoint> fewer;
        fewer.reserve (keep);

        for (size_t i = 0; i < keep; ++i)
            fewer.push_back (points[i * (n - 1) / (keep - 1)]);

        points = std::move (fewer);
    }
}

MotionCurves::MotionCurves (juce::ValueTree& stateRoot, juce::UndoManager* undo, MotionEngine& engine)
    : root (stateRoot), undoManager (undo), motion (engine)
{
    root.addListener (this);
    rasteriseAll();
}

MotionCurves::~MotionCurves()
{
    root.removeListener (this);
}

// ----------------------------------------------------------------------------
std::vector<CurvePoint> MotionCurves::defaultCurve (int lane)
{
    switch ((Lane) lane)
    {
        case Lane::speed:       // slow down, then catch up
            return { { 0.0f, 0.5f, 0.0f }, { 0.5f, 0.2f, 0.4f }, { 1.0f, 0.5f, 0.0f } };
        case Lane::pan:         // left, right, back to centre
            return { { 0.0f, 0.5f, 0.0f }, { 0.25f, 0.05f, 0.0f }, { 0.75f, 0.95f, 0.0f }, { 1.0f, 0.5f, 0.0f } };
        case Lane::feedback:    // swell
            return { { 0.0f, 0.3f, 0.5f }, { 0.7f, 0.95f, 0.0f }, { 1.0f, 0.3f, 0.0f } };
        case Lane::filter:      // closing sweep
            return { { 0.0f, 0.5f, -0.4f }, { 1.0f, 0.1f, 0.0f } };
        case Lane::pitch:       // steps
            return { { 0.0f, 0.5f, 0.0f }, { 0.499f, 0.5f, 0.0f }, { 0.5f, 0.79f, 0.0f }, { 1.0f, 0.79f, 0.0f } };
        case Lane::mix:         // fade in
            return { { 0.0f, 0.15f, 0.3f }, { 1.0f, 0.85f, 0.0f } };
        case Lane::numLanes:
        default:
            return { { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.5f, 0.0f } };
    }
}

float MotionCurves::evaluate (const std::vector<CurvePoint>& points, float x) noexcept
{
    if (points.empty())
        return 0.5f;

    if (x <= points.front().x)
        return points.front().y;

    if (x >= points.back().x)
        return points.back().y;

    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto& a = points[i];
        const auto& b = points[i + 1];

        if (x >= a.x && x <= b.x)
        {
            const float span = b.x - a.x;
            if (span <= 1.0e-6f)
                return b.y;

            float t = (x - a.x) / span;

            // curvature: power curve, symmetric for negative values
            const float c = juce::jlimit (-1.0f, 1.0f, a.curve);
            if (c > 0.001f)        t = std::pow (t, 1.0f + 4.0f * c);
            else if (c < -0.001f)  t = 1.0f - std::pow (1.0f - t, 1.0f - 4.0f * c);

            return a.y + (b.y - a.y) * t;
        }
    }

    return points.back().y;
}

// ----------------------------------------------------------------------------
juce::ValueTree MotionCurves::getMotionTree (bool createIfMissing)
{
    auto tree = root.getChildWithName (motionType);

    if (! tree.isValid() && createIfMissing)
    {
        tree = juce::ValueTree (motionType);
        root.appendChild (tree, nullptr);
    }

    return tree;
}

juce::ValueTree MotionCurves::getLaneTree (int lane, bool createIfMissing)
{
    auto motionTree = getMotionTree (createIfMissing);
    if (! motionTree.isValid())
        return {};

    auto laneTree = motionTree.getChildWithProperty (propLane, lane);

    if (! laneTree.isValid() && createIfMissing)
    {
        laneTree = juce::ValueTree (laneType);
        laneTree.setProperty (propLane, lane, nullptr);
        motionTree.appendChild (laneTree, nullptr);
    }

    return laneTree;
}

std::vector<CurvePoint> MotionCurves::getPoints (int lane) const
{
    const auto motionTree = root.getChildWithName (motionType);
    const auto laneTree   = motionTree.isValid() ? motionTree.getChildWithProperty (propLane, lane) : juce::ValueTree();

    if (! laneTree.isValid() || laneTree.getNumChildren() == 0)
        return defaultCurve (lane);

    std::vector<CurvePoint> points;
    points.reserve ((size_t) juce::jmin (kMaxPoints, laneTree.getNumChildren()));

    for (const auto& pt : laneTree)
    {
        if ((int) points.size() >= kMaxPoints)
            break;                                  // (cannot be: every way in thins the points out)

        CurvePoint p;
        p.x     = usable ((float) pt.getProperty (propX, 0.0f), 0.0f, 1.0f, 0.0f);
        p.y     = usable ((float) pt.getProperty (propY, 0.5f), 0.0f, 1.0f, 0.5f);
        p.curve = usable ((float) pt.getProperty (propCurve, 0.0f), -1.0f, 1.0f, 0.0f);
        points.push_back (p);
    }

    std::stable_sort (points.begin(), points.end(), [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    return points;
}

void MotionCurves::setPoints (int lane, const std::vector<CurvePoint>& given, bool undoable)
{
    auto* um = undoable ? undoManager : nullptr;
    auto laneTree = getLaneTree (lane, true);

    auto points = given;
    thinOut (points);

    ++suspend;
    laneTree.removeAllChildren (um);

    for (const auto& raw : points)
    {
        const auto p = usable (raw);

        juce::ValueTree pt (pointType);
        pt.setProperty (propX, p.x, nullptr);
        pt.setProperty (propY, p.y, nullptr);
        pt.setProperty (propCurve, p.curve, nullptr);
        laneTree.appendChild (pt, um);
    }

    --suspend;
    rasterise (lane);
}

juce::ValueTree MotionCurves::cleaned (const juce::XmlElement& motionXml)
{
    juce::ValueTree out (motionType);

    if (! motionXml.hasTagName (motionType.toString()))
        return out;

    // a number as the file gives it: text, which may read "nan", "inf" or 1e308
    const auto number = [] (const juce::XmlElement& e, const juce::Identifier& name, float fallback)
    {
        if (! e.hasAttribute (name.toString()))
            return fallback;

        const double v = e.getStringAttribute (name.toString()).getDoubleValue();
        return std::isfinite (v) && std::abs (v) <= 1.0e6 ? (float) v : std::numeric_limits<float>::quiet_NaN();
    };

    for (int l = 0; l < kNumLanes; ++l)
    {
        const juce::XmlElement* laneXml = nullptr;

        for (auto* e : motionXml.getChildWithTagNameIterator (laneType.toString()))
            if (laneXml == nullptr && e->getStringAttribute (propLane.toString()).trim() == juce::String (l))
                laneXml = e;

        if (laneXml == nullptr)
            continue;                               // (the lane then has its default curve)

        std::vector<CurvePoint> points;

        for (auto* pt : laneXml->getChildWithTagNameIterator (pointType.toString()))
            points.push_back (usable (CurvePoint { number (*pt, propX, 0.0f), number (*pt, propY, 0.5f), number (*pt, propCurve, 0.0f) }));

        thinOut (points);

        juce::ValueTree lane (laneType);
        lane.setProperty (propLane, l, nullptr);

        for (const auto& p : points)
        {
            juce::ValueTree pt (pointType);
            pt.setProperty (propX, p.x, nullptr);
            pt.setProperty (propY, p.y, nullptr);
            pt.setProperty (propCurve, p.curve, nullptr);
            lane.appendChild (pt, nullptr);
        }

        out.appendChild (lane, nullptr);
    }

    return out;
}

void MotionCurves::restoreFrom (const juce::ValueTree& motionTree, bool undoable)
{
    for (int l = 0; l < kNumLanes; ++l)
    {
        std::vector<CurvePoint> points;
        const auto laneTree = motionTree.isValid() ? motionTree.getChildWithProperty (propLane, l) : juce::ValueTree();

        if (laneTree.isValid())
        {
            for (const auto& pt : laneTree)
                points.push_back ({ (float) pt.getProperty (propX, 0.0f),
                                    (float) pt.getProperty (propY, 0.5f),
                                    (float) pt.getProperty (propCurve, 0.0f) });
        }

        if (points.empty())
            points = defaultCurve (l);

        setPoints (l, points, undoable);
    }
}

juce::ValueTree MotionCurves::createCopy() const
{
    juce::ValueTree copy (motionType);

    for (int l = 0; l < kNumLanes; ++l)
    {
        juce::ValueTree laneTree (laneType);
        laneTree.setProperty (propLane, l, nullptr);

        for (const auto& p : getPoints (l))
        {
            juce::ValueTree pt (pointType);
            pt.setProperty (propX, p.x, nullptr);
            pt.setProperty (propY, p.y, nullptr);
            pt.setProperty (propCurve, p.curve, nullptr);
            laneTree.appendChild (pt, nullptr);
        }

        copy.appendChild (laneTree, nullptr);
    }

    return copy;
}

void MotionCurves::stateReplaced()
{
    rasteriseAll();
}

// ----------------------------------------------------------------------------
void MotionCurves::rasterise (int lane)
{
    if (lane < 0 || lane >= kNumLanes)
        return;

    const auto points = getPoints (lane);

    for (int i = 0; i < MotionEngine::kTableSize; ++i)
    {
        const float x = (float) i / (float) (MotionEngine::kTableSize - 1);
        motion.setTableValue (lane, i, evaluate (points, x) * 2.0f - 1.0f);
    }

    if (onCurveChanged != nullptr)
        onCurveChanged (lane);
}

void MotionCurves::rasteriseAll()
{
    for (int l = 0; l < kNumLanes; ++l)
        rasterise (l);
}

int MotionCurves::laneOf (const juce::ValueTree& tree) const
{
    if (tree.hasType (laneType))
        return (int) tree.getProperty (propLane, -1);

    if (tree.hasType (pointType))
        return laneOf (tree.getParent());

    return -1;
}

void MotionCurves::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (suspend == 0 && tree.hasType (pointType))
        rasterise (laneOf (tree));
}

void MotionCurves::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (suspend != 0)
        return;

    if (child.hasType (motionType) || child.hasType (laneType))   rasteriseAll();
    else if (child.hasType (pointType))                           rasterise (laneOf (parent));
}

void MotionCurves::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (suspend != 0)
        return;

    if (child.hasType (motionType) || child.hasType (laneType))   rasteriseAll();
    else if (child.hasType (pointType))                           rasterise (laneOf (parent));
}

void MotionCurves::valueTreeRedirected (juce::ValueTree&)
{
    rasteriseAll();
}

} // namespace dlay

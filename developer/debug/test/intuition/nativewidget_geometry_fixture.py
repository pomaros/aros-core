# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Licensed under the AROS Public License (APL) Version 1.1.
"""Actual Intuition constructor/limit bodies shared by native widget fixtures."""
import re

def function(text, name):
    match = re.search(r'(?m)^(?:static\s+)?(?:BOOL|void)\s+' + name + r'\(', text)
    if not match:
        raise RuntimeError(name)
    brace = text.index('{', match.start())
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]

def header(root):
    return (root / 'rom/intuition/awaregeometry.h').read_text().replace('#include <exec/types.h>', '')

def bodies(root):
    source = (root / 'rom/intuition/windowfit.c').read_text()
    limits = (root / 'rom/intuition/windowlimits.c').read_text()
    start = limits.index('    AROS_LIBFUNC_INIT')
    end = limits.index('} /* WindowLimits */', start)
    return r'''
#define IW(w) (w)
#define IS(s) (s)
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT
#define DEBUG_WINDOWLIMITS(x)
#define SANITY_CHECKR(w,value) if(!(w))return value;
static ULONG fixture_ibase_lock;
static ULONG LockIBase(ULONG ignored) {(void)ignored;assert(!fixture_ibase_lock);return ++fixture_ibase_lock;}
static void UnlockIBase(ULONG lock) {assert(lock==1&&fixture_ibase_lock==1);--fixture_ibase_lock;}
''' + function(source, 'WindowFitInitializeGeometry') + '\n' + function(source, 'WindowFitRecordLimits') + '\n' + r'''
static BOOL WindowLimits(struct Window *window,WORD MinWidth,WORD MinHeight,UWORD MaxWidth,UWORD MaxHeight)
{
''' + limits[start:end] + '}\n' + r'''
/* Real canonical plans after the actual constructor/defaulting. Caller updates
 * are checked separately through actual WindowLimits/WindowFitRecordLimits. */
static void VerifyWidgetGeometry(const struct Window *window)
{
    struct AwareGeometry original = window->ScaleGeometry;
    ULONG birth = window->WScreen->Scale;
    for (ULONG target = 100; target <= 400; target += 25)
    {
        struct AwareGeometryFrame frame = {
            (window->BorderLeft * target + birth/2)/birth,
            (window->BorderTop * target + birth/2)/birth,
            (window->BorderRight * target + birth/2)/birth,
            (window->BorderBottom * target + birth/2)/birth};
        struct AwareGeometryPlan plan;
        if (!aware_geometry_prepare(&window->ScaleGeometry,target,
                window->WScreen->Width,window->WScreen->Height,&frame,&plan))
        {
            fprintf(stderr,"actual constructor live refusal birth=%u target=%u minimum=%d,%d anchor=%u,%u\n",
                birth,target,original.axis[AG_MINWIDTH].value,original.axis[AG_MINHEIGHT].value,
                original.axis[AG_MINWIDTH].scale,original.axis[AG_MINHEIGHT].scale);
            abort();
        }
        assert(plan.minwidth>0&&plan.minheight>0&&plan.maxwidth>=plan.minwidth&&plan.maxheight>=plan.minheight);
        assert(plan.width>frame.left+frame.right&&plan.height>frame.top+frame.bottom);
        assert(plan.left>=0&&plan.top>=0&&plan.left+plan.width<=window->WScreen->Width&&plan.top+plan.height<=window->WScreen->Height);
        if(target==birth)assert(plan.width==window->Width&&plan.height==window->Height&&plan.left==window->LeftEdge&&plan.top==window->TopEdge);
        assert(!memcmp(&original,&window->ScaleGeometry,sizeof original));
    }
    /* Explicit reset after the 400% plan, including windows born at 200%. */
    struct AwareGeometryFrame birthframe = {window->BorderLeft,window->BorderTop,
        window->BorderRight,window->BorderBottom};
    struct AwareGeometryPlan reset;
    assert(aware_geometry_prepare(&window->ScaleGeometry,birth,
        window->WScreen->Width,window->WScreen->Height,&birthframe,&reset));
    assert(reset.left==window->LeftEdge&&reset.top==window->TopEdge&&
        reset.width==window->Width&&reset.height==window->Height);
    assert(!memcmp(&original,&window->ScaleGeometry,sizeof original));
}
'''

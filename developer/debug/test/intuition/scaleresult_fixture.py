# Copyright (C) 2026 John KNIPPER <code@jkn.me>.
# Distributed under the AROS Public License, version 1.1.
"""Extract the public receipt and real private producer for host controls."""
def result_definitions(root):
    source=(root/'compiler/include/intuition/screens.h').read_text(encoding='latin1')
    start=source.index('#define SA_ScaleResult')
    end=source.index('#define SA_SCALE_MIN',start)
    return source[start:end]+'\n'
def result_helpers(root):
    return '\n'.join(line for line in (root/'rom/intuition/scaleresult.h').read_text().splitlines() if not line.startswith('#include'))+'\n'
def detail_header(root):
    """The shared refusal wording (scaledetail.h) with its decorator constants."""
    import re
    decor=(root/'compiler/include/intuition/windecorclass.h').read_text(encoding='latin1')
    keep=[l.split('/*')[0].rstrip() for l in decor.splitlines() if re.match(r'#define\s+(WDA_Dummy|WDM_Dummy|WDM_\w*SCALE\w*|WDP_REFUSE_\w+)\s',l)]
    # The Layers refusal and HIDD pixel-lease causes it names.
    for header in ('compiler/include/graphics/layertransaction.h','rom/hidds/gfx/include/preparedpixels.h'):
        keep+=[l.split('/*')[0].rstrip() for l in (root/header).read_text().splitlines()
               if re.match(r'#define\s+(LGR_|LGRF_|HIDD_PIXELS_REFUSE_|HIDD_SCANOUT_|HIDD_CURSOR_REFUSE_)\w*\s',l)]
    text=(root/'compiler/include/intuition/scaledetail.h').read_text()
    body='\n'.join(l for l in text.splitlines() if not l.startswith('#include'))
    # A host model may already define a method id; never redefine it.
    guarded=''.join('#ifndef %s\n%s\n#endif\n'%(l.split()[1],l) for l in keep)
    return '\n#ifndef TAG_USER\n#define TAG_USER 0x80000000U\n#endif\n'+guarded+body+'\n'

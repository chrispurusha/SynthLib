# synthlibCtlPoints.h

## 1. Performance controllers at their own frame
A host hands a plug-in without paramPoints() only a block's last value of each parameter, so a fast wheel, bend or
pressure moves once a block - 3 to 10 ms apart in Live, heard as steps. A plug-in that implements paramPoints() keeps
those controllers' points here (synthlib_ctl_points_add, any other parameter settling at once through its own
setParam), and its process loop, rendering up to each note event, also stops at each point's frame: renders up to it,
calls its own setParam with the value, rebuilds anything it builds once a block, and goes on
(synthlib_ctl_points_due). First in Nl3Alike for its wheel (2026-11-16, owner: rapid wheel moves stepped), then the
other Alikes for wheel, bend and pressure. A header, not a source file: each plug-in's loop is its own.

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Samsung MFC (Multi Function Codec - FIMV) tracepoints
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM s5p_mfc

#if !defined(_S5P_MFC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _S5P_MFC_TRACE_H

#include <linux/device.h>
#include <linux/tracepoint.h>

#include "s5p_mfc_common.h"

DECLARE_EVENT_CLASS(s5p_mfc_reg,
	TP_PROTO(struct s5p_mfc_dev *dev, const void __iomem *addr, u32 val),
	TP_ARGS(dev, addr, val),
	TP_STRUCT__entry(
		__string(dev, dev_name(&dev->plat_dev->dev))
		__field(u32, offset)
		__field(u32, val)
	),
	TP_fast_assign(
		__assign_str(dev);
		__entry->offset = addr - dev->regs_base;
		__entry->val = val;
	),
	TP_printk("%s: %#06x = %#010x", __get_str(dev), __entry->offset,
		  __entry->val)
);

DEFINE_EVENT(s5p_mfc_reg, s5p_mfc_reg_read,
	TP_PROTO(struct s5p_mfc_dev *dev, const void __iomem *addr, u32 val),
	TP_ARGS(dev, addr, val)
);

DEFINE_EVENT(s5p_mfc_reg, s5p_mfc_reg_write,
	TP_PROTO(struct s5p_mfc_dev *dev, const void __iomem *addr, u32 val),
	TP_ARGS(dev, addr, val)
);

#endif /* _S5P_MFC_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE s5p_mfc_trace

#include <trace/define_trace.h>

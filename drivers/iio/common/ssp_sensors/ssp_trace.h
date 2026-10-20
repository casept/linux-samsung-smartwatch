/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Trace events of the transfers between the AP and the sensorhub MCU
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ssp_sensors

#if !defined(_SSP_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _SSP_TRACE_H

#include <linux/device.h>
#include <linux/minmax.h>
#include <linux/tracepoint.h>

/* More than any frame the MCU sends, and still fits into a printed line */
#define SSP_TRACE_MAX_PAYLOAD	1024

#define ssp_trace_len(len)	min_t(unsigned int, len, SSP_TRACE_MAX_PAYLOAD)
/* Marks a payload cut to SSP_TRACE_MAX_PAYLOAD */
#define ssp_trace_more(len, field) \
	((len) > __get_dynamic_array_len(field) ? " ..." : "")

TRACE_EVENT(ssp_msg_start,
	TP_PROTO(struct device *dev, u8 cmd, u16 options, u32 arg,
		 const void *payload, unsigned int len),
	TP_ARGS(dev, cmd, options, arg, payload, len),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(u8, cmd)
		__field(u16, options)
		__field(u32, arg)
		__field(unsigned int, len)
		__dynamic_array(u8, payload, ssp_trace_len(len))
	),
	TP_fast_assign(
		__assign_str(dev);
		__entry->cmd = cmd;
		__entry->options = options;
		__entry->arg = arg;
		__entry->len = len;
		memcpy(__get_dynamic_array(payload), payload,
		       __get_dynamic_array_len(payload));
	),
	TP_printk("%s cmd=0x%02x options=0x%x arg=%u len=%u payload=%s%s",
		  __get_str(dev), __entry->cmd, __entry->options, __entry->arg,
		  __entry->len,
		  __print_hex(__get_dynamic_array(payload),
			      __get_dynamic_array_len(payload)),
		  ssp_trace_more(__entry->len, payload))
);

TRACE_EVENT(ssp_msg_done,
	TP_PROTO(struct device *dev, u8 cmd, u16 options, int ret,
		 const void *payload, unsigned int len),
	TP_ARGS(dev, cmd, options, ret, payload, len),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(u8, cmd)
		__field(u16, options)
		__field(int, ret)
		__field(unsigned int, len)
		__dynamic_array(u8, payload, ssp_trace_len(len))
	),
	TP_fast_assign(
		__assign_str(dev);
		__entry->cmd = cmd;
		__entry->options = options;
		__entry->ret = ret;
		__entry->len = len;
		memcpy(__get_dynamic_array(payload), payload,
		       __get_dynamic_array_len(payload));
	),
	TP_printk("%s cmd=0x%02x options=0x%x ret=%d len=%u payload=%s%s",
		  __get_str(dev), __entry->cmd, __entry->options, __entry->ret,
		  __entry->len,
		  __print_hex(__get_dynamic_array(payload),
			      __get_dynamic_array_len(payload)),
		  ssp_trace_more(__entry->len, payload))
);

TRACE_EVENT(ssp_rx_header,
	TP_PROTO(struct device *dev, u16 options, u16 len),
	TP_ARGS(dev, options, len),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(u16, options)
		__field(u16, len)
	),
	TP_fast_assign(
		__assign_str(dev);
		__entry->options = options;
		__entry->len = len;
	),
	TP_printk("%s options=0x%x len=%u", __get_str(dev), __entry->options,
		  __entry->len)
);

TRACE_EVENT(ssp_rx_frame,
	TP_PROTO(struct device *dev, const void *frame, unsigned int len),
	TP_ARGS(dev, frame, len),
	TP_STRUCT__entry(
		__string(dev, dev_name(dev))
		__field(unsigned int, len)
		__dynamic_array(u8, frame, ssp_trace_len(len))
	),
	TP_fast_assign(
		__assign_str(dev);
		__entry->len = len;
		memcpy(__get_dynamic_array(frame), frame,
		       __get_dynamic_array_len(frame));
	),
	TP_printk("%s len=%u frame=%s%s", __get_str(dev), __entry->len,
		  __print_hex(__get_dynamic_array(frame),
			      __get_dynamic_array_len(frame)),
		  ssp_trace_more(__entry->len, frame))
);

#endif /* _SSP_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE ssp_trace
#include <trace/define_trace.h>

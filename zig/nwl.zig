//! nwl, for Zig!
//! Top tip! In root do something like...
//! pub const wayland = @import("wayland");
//! Then Wayland types here will become zig-wayland types!

const std = @import("std");

// Maybe remove the "Nwl" prefix from type names since this language has namespacing?
// Gotta make this nicely zigged up..
const NwlGlobalImpl = extern struct {
    destroy: ?*const fn (?*anyopaque) callconv(.C) void,
};
pub const NwlGlobal = extern struct {
    link: WlList,
    name: u32,
    global: ?*anyopaque,
    impl: NwlGlobalImpl,
};

/// CamelCasify a Wayland object name, to match zig-wayland
const WlObjectNamer = struct {
    fn doWrite(name:[]const u8, writer:anytype) !void {
        var tok = std.mem.tokenize(u8, name, "_");
        while (tok.next()) |t| {
            try writer.writeByte(std.ascii.toUpper(t[0]));
            try writer.writeAll(t[1..]);
        }
    }
    pub fn casify(comptime name:[]const u8) []const u8 {
        var cw = std.io.countingWriter(std.io.null_writer);
        WlObjectNamer.doWrite(name, cw.writer()) catch unreachable;
        var buf:[cw.bytes_written]u8 = undefined;
        var stream = std.io.fixedBufferStream(buf[0..]);
        WlObjectNamer.doWrite(name, stream.writer()) catch unreachable;
        return buf[0..];
    }
};

/// Make a Wayland type, either a opaque or zig-wayland type
fn WaylandObject(comptime name:[]const u8) type {
    const root = @import("root");
    if (@hasDecl(root, "wayland")) {
        const client = root.wayland.client;
        const delim = std.mem.indexOf(u8, name, "_");
        if (delim) |d| {
            const prefix = name[0..d];
            if (@hasDecl(client, prefix)) {
                const sub = @field(client, prefix);
                const camelname = WlObjectNamer.casify(name[d+1..]);
                if (@hasDecl(sub, camelname)) {
                    return @field(sub, camelname);
                }
            }
        }
        else if (@hasDecl(client, name)) {
            return @field(client, name);
        }
    }
    return opaque {};
}

pub const WlDisplay = WaylandObject("wl_display");
pub const WlRegistry = WaylandObject("wl_registry");
pub const WlCompositor = WaylandObject("wl_compositor");
pub const WlShm = WaylandObject("wl_shm");
pub const WlOutput = WaylandObject("wl_output");
pub const ZxdgOutputV1 = WaylandObject("zxdg_output_v1");

pub const XdgWmBase = WaylandObject("xdg_wm_base");
pub const ZxdgOutputManagerV1 = WaylandObject("zxdg_output_manager_v1");
pub const ZwlrLayerShellV1 = WaylandObject("zwlr_layer_shell_v1");
pub const ZxdgDecorationManagerV1 = WaylandObject("zxdg_decoration_manager_v1");
pub const WpViewporter = WaylandObject("wp_viewporter");
pub const WlSubcompositor = WaylandObject("wl_subcompositor");
pub const WlDataDeviceManager = WaylandObject("wl_data_device_manager");
pub const WlBuffer = WaylandObject("wl_buffer");
pub const WlCallback = WaylandObject("wl_callback");
pub const WlDataDevice = WaylandObject("wl_data_device");
pub const WlDataOffer = WaylandObject("wl_data_offer");
pub const WlDataSource = WaylandObject("wl_data_source");
pub const WlKeyboard = WaylandObject("wl_keyboard");
pub const WlPointer = WaylandObject("wl_pointer");
pub const WlProxy = opaque {};
pub const WlRegion = WaylandObject("wl_region");
pub const WlSeat = WaylandObject("wl_seat");
pub const WlShmPool = WaylandObject("wl_shm_pool");
pub const WlSubsurface = WaylandObject("wl_subsurface");
pub const WlSurface = WaylandObject("wl_surface");
pub const WlTouch = WaylandObject("wl_touch");
pub const XdgSurface = WaylandObject("xdg_surface");
pub const ZxdgToplevelDecorationV1 = WaylandObject("zxdg_toplevel_decoration_v1");
pub const WpViewport = WaylandObject("wp_viewport");
pub const XdgPositioner = WaylandObject("xdg_positioner");

pub const XdgToplevel = WaylandObject("xdg_toplevel");
pub const XdgPopup = WaylandObject("xdg_popup");
pub const ZwlrLayerSurfaceV1 = WaylandObject("zwlr_layer_surface_v1");

pub const XkbContext = opaque {};
pub const WlCursorTheme = opaque {};
pub const NwlPoll = opaque {};
const NwlStateEventsImpl = extern struct {
    output_new: ?*const fn (*NwlOutput) callconv(.C) void = null,
    output_destroy: ?*const fn (*NwlOutput) callconv(.C) void = null,
    global_add: ?*const fn (*NwlState, *const WlRegistry, u32, [*:0]const u8, u32) callconv(.C) bool = null,
    global_remove: ?*const fn (*NwlState, *const WlRegistry, u32) callconv(.C) void = null,
};

pub const NwlOutput = extern struct {
    state: *NwlState,
    output: *WlOutput,
    xdg_output: ?*ZxdgOutputV1,
    link: WlList,
    scale: c_int,
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    name: ?[*:0]u8,
};
pub const NwlStateSubImpl = extern struct {
    destroy: ?*const fn (?*anyopaque) callconv(.C) void,
};
pub const NwlStateSub = extern struct {
    link: WlList,
    data: ?*anyopaque,
    impl: *NwlStateSubImpl,
};

// TODO: figure out if it's possible to make this typed.
pub const WlList = extern struct {
    prev: *WlList = undefined,
    next: *WlList = undefined,
};

pub fn WlListHead(comptime linktype:type, comptime field: std.meta.FieldEnum(linktype)) type {
    const tinfo = @typeInfo(linktype);
    if (tinfo != .Struct) {
        @compileError("expected struct, got " ++ .{@typeName(linktype)});
    }
    const fieldinfo = std.meta.fieldInfo(linktype, field);
    if (fieldinfo.type != WlList) {
        @compileError(fieldinfo.name ++ " is a " ++ @typeName(fieldinfo.type) ++ ", not a WlList!");
    }
    return extern struct{
        prev: *WlList = undefined,
        next: *WlList = undefined,
        const Iterator = struct {
            this:*WlList,
            pos:*WlList,

            pub fn next(it:*Iterator) ?*linktype {
                if (it.this != it.pos) {
                    const ret = it.pos;
                    it.pos = it.pos.next;
                    return @fieldParentPtr(linktype, fieldinfo.name, ret);
                }
                return null;
            }
        };
        pub fn iterator(self:*@This()) Iterator {
            return .{
                .this = @ptrCast(*WlList, self),
                .pos = self.next
            };
        }
    };
}

const NwlSurfaceWlObjects = extern struct {
    surface: *WlSurface,
    xdg_surface: ?*XdgSurface,
    viewport: ?*WpViewport,
    frame_cb: ?*WlCallback,
};
pub const nwl_surface_generic_func_t = *const fn (*NwlSurface) callconv(.C) void;
pub const NwlRendererImpl = extern struct {
    apply_size: nwl_surface_generic_func_t,
    surface_destroy: nwl_surface_generic_func_t,
    swap_buffers: *const fn (*NwlSurface, i32, i32) callconv(.C) void,
    render: nwl_surface_generic_func_t,
    destroy: nwl_surface_generic_func_t,
};
pub const NwlRenderer = extern struct {
    impl: ?*const NwlRendererImpl,
    data: ?*anyopaque,
    rendering: bool,
};
pub const WlArray = extern struct {
    size: usize,
    alloc: usize,
    data: ?*anyopaque,
};

pub const WlFixed = enum(i32) {
    _,
    pub fn toDouble(f: WlFixed) f64 {
        const i = @as(i64, ((1023 + 44) << 52) + (1 << 51)) + @enumToInt(f);
        return @bitCast(f64, i) - (3 << 43);
    }
    pub fn fromDouble(d: f64) WlFixed {
        const i = d + (3 << (51 - 8));
        return @intToEnum(WlFixed, @bitCast(i64, i));
    }
    pub fn toInt(self: WlFixed) c_int {
        return @divTrunc(@enumToInt(self), @as(c_int, 256));
    }
    pub fn fromInt(arg_i: c_int) WlFixed {
        return arg_i * @as(c_int, 256);
    }

    pub fn format(value: WlFixed, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        try std.fmt.formatType(value.toDouble(), fmt, options, writer, 1);
    }
};

pub const xkb_keysym_t = u32;
pub const xkb_keycode_t = u32;

const NwlSurfaceFlags = packed struct(u32) {
    no_autoscale: bool,
    no_autocursor: bool,
    nwl_frees: bool,
    padding: u29
};

const NwlSurfaceState = packed struct(u32) {
    active: bool,
    maximized: bool,
    fullscreen: bool,
    resizing: bool,
    tiled_left: bool,
    tiled_right: bool,
    tiled_top: bool,
    tiled_bottom: bool,
    csd: bool,
    needs_draw: bool,
    needs_applysize: bool,
    destroy: bool,
    needs_configure: bool,
    _padding:u19
};

const NwlXdgWmCaps = packed struct(u8) {
    window_menu: bool,
    maximize: bool,
    fullscreen: bool,
    minimize: bool,
    _pad:u4
};

const NwlSurfaceRoleUnion = extern union {
    toplevel: extern struct {
        wl: *XdgToplevel,
        decoration: ?*ZxdgToplevelDecorationV1,
        wm_capabilities:NwlXdgWmCaps,
        bounds_width:i32,
        bounds_height:i32
    },
    popup: extern struct {
        wl: *XdgPopup,
        lx:i32,
        ly:i32,
        reposition_token:u32
    },
    layer: extern struct {
        wl: *ZwlrLayerSurfaceV1
    },
    subsurface: extern struct {
        wl: *WlSubsurface
    },
};

pub const nwl_surface_destroy_t = ?*const fn (*NwlSurface) callconv(.C) void;
pub const nwl_surface_input_pointer_t = ?*const fn (*NwlSurface, *NwlSeat, *NwlPointerEvent) callconv(.C) void;
pub const nwl_surface_input_keyboard_t = ?*const fn (*NwlSurface, *NwlSeat, *NwlKeyboardEvent) callconv(.C) void;
pub const nwl_surface_configure_t = ?*const fn (*NwlSurface, u32, u32) callconv(.C) void;
pub const nwl_surface_dnd_t = ?*const fn(*NwlSurface, *NwlSeat, *NwlDndEvent) callconv(.C) void;

const NwlSurfaceImpl = extern struct {
    destroy: nwl_surface_destroy_t,
    input_pointer: nwl_surface_input_pointer_t,
    input_keyboard: nwl_surface_input_keyboard_t,
    dnd: nwl_surface_dnd_t,
    configure: nwl_surface_configure_t,
    close: ?*const fn (*NwlSurface) callconv(.C) void,
};

const NwlSurfaceRoleId = enum(u8) { none, toplevel, popup, layer, sub, cursor, dragicon };

const NwlSurfaceOutputs = extern struct {
    outputs: [*]*NwlOutput,
    amount: u32,
};

const NwlDataDevice = extern struct {
    wl: ?*WlDataDevice,
    drop: DataOffer,
    selection: DataOffer,
    incoming: DataOffer,
    event: NwlDndEvent,
};
pub const struct_xkb_keymap = opaque {};
pub const struct_xkb_state = opaque {};
pub const struct_xkb_compose_state = opaque {};
pub const struct_xkb_compose_table = opaque {};
pub const NwlKeyboardEvent = extern struct {
    type: NwlKeyboardEventType,
    compose_state: NwlKeyboardComposeState,
    focus: bool,
    keysym: xkb_keysym_t,
    keycode: xkb_keycode_t,
    utf8: [16]u8,
    serial: u32,
};
pub const WlCursor = opaque {};

const NwlPointerEventChanged = packed struct(u8) { focus: bool, button: bool, motion: bool, axis: bool, _padding: u4 };

const NwlPointerButtons = packed struct(u8) { left: bool, middle: bool, right: bool, back: bool, forward: bool, _padding: u3 };

const NwlPointerAxisSource = packed struct(u8) {
    wheel:bool,
    finger:bool,
    continuos:bool,
    wheel_tilt:bool,
    _pad:u4
};

const NwlPointerAxis = packed struct(u8) {
    vertical:bool,
    horizontal:bool,
    _pad:u6
};

pub const NwlPointerEvent = extern struct {
    changed: NwlPointerEventChanged,
    serial: u32,
    surface_x: WlFixed,
    surface_y: WlFixed,
    buttons: NwlPointerButtons,
    buttons_prev: NwlPointerButtons,
    axis_value_vert: i32,
    axis_value_hori: i32,
    axis_hori: WlFixed,
    axis_vert: WlFixed,
    axis_source: NwlPointerAxisSource,
    axis_stop: NwlPointerAxis,
    focus: bool,
};

const NwlPointerSurface = extern struct {
    xcursor: ?*WlCursor,
    xcursor_surface: ?*WlSurface,
    nwl: ?*NwlSurface,
    hot_x: i32,
    hot_y: i32,
};

pub const NwlSeat = extern struct {
    state: *NwlState,
    link: WlList,
    wl_seat: *WlSeat,
    data_device: NwlDataDevice,
    keyboard: ?*WlKeyboard,
    keyboard_keymap: ?*struct_xkb_keymap,
    keyboard_state: ?*struct_xkb_state,
    keyboard_context: ?*XkbContext,
    keyboard_compose_enabled: bool,
    keyboard_repeat_enabled: bool,
    keyboard_compose_state: ?*struct_xkb_compose_state,
    keyboard_compose_table: ?*struct_xkb_compose_table,
    keyboard_focus: ?*NwlSurface,
    keyboard_repeat_rate: i32,
    keyboard_repeat_delay: i32,
    keyboard_repeat_fd: c_int,
    keyboard_event: ?*NwlKeyboardEvent,
    touch: ?*WlTouch,
    touch_focus: ?*NwlSurface,
    touch_serial: u32,
    pointer: ?*WlPointer,
    pointer_focus: ?*NwlSurface,
    pointer_prev_focus: ?*NwlSurface,
    pointer_surface: NwlPointerSurface,
    pointer_event: ?*NwlPointerEvent,
    name: [*:0]u8,
};

pub const NwlDndEventType = enum(u8) {
    motion = 0,
    enter,
    left,
    drop
};

pub const NwlDndEvent = extern struct {
    type: NwlDndEventType,
    serial: u32,
    focus_surface: ?*NwlSurface,
    x: WlFixed,
    y: WlFixed,
    time: u32,
    source_actions: u32,
    action: u32,
};

pub const DataOffer = extern struct {
    mime: WlArray,
    offer: ?*WlDataOffer,
};

const NwlKeyboardEventType = enum(u8) {
    focus,
    keydown,
    keyup,
    keyrepeat,
    modifiers,
};
const NwlKeyboardComposeState = enum(u8) { none, composing, composed };

pub const nwl_poll_callback_t = *const fn (*NwlState, ?*anyopaque) callconv(.C) void;

pub const cairo_surface_t = opaque {};
pub const nwl_surface_cairo_render_t = *const fn (*NwlSurface, *cairo_surface_t) callconv(.C) void;
extern fn nwl_surface_renderer_cairo(surface: *NwlSurface, egl: bool, renderfunc: nwl_surface_cairo_render_t) void;
pub const nwlSurfaceRendererCairo = nwl_surface_renderer_cairo;

extern fn wl_proxy_marshal(p: ?*WlProxy, opcode: u32, ...) void;

const NwlError = error{ InitFailed, RoleSetFailed, SurfaceCreateFailed };

const NwlSurfaceEvents = struct { destroy: ?*const fn (*const NwlSurface) void = null };

pub const NwlSurface = extern struct {
    link: WlList,
    dirtlink: WlList,
    state: *NwlState,
    wl: NwlSurfaceWlObjects,
    renderer: NwlRenderer,
    width: u32,
    height: u32,
    desired_width: u32,
    desired_height: u32,
    current_width: u32,
    current_height: u32,
    configure_serial: u32,
    scale: c_int,
    parent: ?*NwlSurface,
    subsurfaces: WlList,
    outputs: NwlSurfaceOutputs,
    flags: NwlSurfaceFlags,
    states: NwlSurfaceState,
    title: ?[*:0]u8,
    role_id: NwlSurfaceRoleId,
    role: NwlSurfaceRoleUnion,
    frame: u32,
    impl: NwlSurfaceImpl,
    userdata: ?*anyopaque,
    extern fn nwl_surface_destroy(surface: *NwlSurface) void;
    extern fn nwl_surface_destroy_later(surface: *NwlSurface) void;
    extern fn nwl_surface_set_vp_destination(surface: *NwlSurface, width: i32, height: i32) bool;
    extern fn nwl_surface_set_size(surface: *NwlSurface, width: u32, height: u32) void;
    extern fn nwl_surface_set_title(surface: *NwlSurface, title:?[*:0]const u8) void;
    extern fn nwl_surface_swapbuffers(surface: *NwlSurface, x: i32, y: i32) void;
    extern fn nwl_surface_render(surface: *NwlSurface) void;
    extern fn nwl_surface_set_need_draw(surface: *NwlSurface, rendernow: bool) void;
    extern fn nwl_surface_role_subsurface(surface: *NwlSurface, parent: *NwlSurface) bool;
    extern fn nwl_surface_role_layershell(surface: *NwlSurface, output: ?*WlOutput, layer: u32) bool;
    extern fn nwl_surface_role_toplevel(surface: *NwlSurface) bool;
    extern fn nwl_surface_role_popup(surface: *NwlSurface, parent: *NwlSurface, positioner: *XdgPositioner) bool;
    extern fn nwl_surface_role_unset(surface: *NwlSurface) void;
    extern fn nwl_surface_init(surface: *NwlSurface, state: *NwlState, title:[*:0]const u8) void;

    pub fn commit(self: *NwlSurface) void {
        if (@hasDecl(WlSurface, "commit")) {
            self.wl.surface.commit();
        } else {
            wl_proxy_marshal(@ptrCast(*WlProxy, self.wl.surface), @as(u32, 6));
        }
    }
    pub fn setSize(self: *NwlSurface, width: u32, height: u32) void {
        nwl_surface_set_size(self, width, height);
    }
    pub fn roleTopLevel(self: *NwlSurface) NwlError!void {
        if (!nwl_surface_role_toplevel(self)) {
            return NwlError.RoleSetFailed;
        }
    }
    pub fn rolePopup(self: *NwlSurface, parent:*NwlSurface, positioner:*XdgPositioner) NwlError!void {
        if (!nwl_surface_role_popup(self, parent, positioner)) {
            return NwlError.RoleSetFailed;
        }
    }
    pub fn roleLayershell(self: *NwlSurface, output: ?*WlOutput, layer: u32) NwlError!void {
        if (!nwl_surface_role_layershell(self, output, layer)) {
            return NwlError.RoleSetFailed;
        }
    }
    pub fn roleSubsurface(self: *NwlSurface, parent: *NwlSurface) NwlError!void {
        if (!nwl_surface_role_subsurface(self, parent)) {
            return NwlError.RoleSetFailed;
        }
    }
    pub fn render(self: *NwlSurface) void {
        nwl_surface_render(self);
    }
    pub const setTitle = nwl_surface_set_title;
    pub const setVpDestination = nwl_surface_set_vp_destination;
    pub fn swapBuffers(self: *NwlSurface, x: i32, y: i32) void {
        nwl_surface_swapbuffers(self, x, y);
    }
    pub fn setNeedDraw(self: *NwlSurface, rendernow: bool) void {
        nwl_surface_set_need_draw(self, rendernow);
    }
    pub fn destroy(self: *NwlSurface) void {
        nwl_surface_destroy(self);
    }
    pub fn destroyLater(self: *NwlSurface) void {
        nwl_surface_destroy_later(self);
    }
    pub fn unsetRole(self: *NwlSurface) void {
        nwl_surface_role_unset(self);
    }
    pub const init = nwl_surface_init;

    /// Convenience function for casting userdata.
    pub inline fn castData(self: *const NwlSurface, comptime datatype: type) *datatype {
        return @ptrCast(*datatype, @alignCast(@alignOf(datatype), self.userdata));
    }
};

const NwlStateWlObjects = extern struct {
    display: ?*WlDisplay = null,
    registry: ?*WlRegistry = null,
    compositor: ?*WlCompositor = null,
    shm: ?*WlShm = null,
    xdg_wm_base: ?*XdgWmBase = null,
    xdg_output_manager: ?*ZxdgOutputManagerV1 = null,
    layer_shell: ?*ZwlrLayerShellV1 = null,
    decoration: ?*ZxdgDecorationManagerV1 = null,
    viewporter: ?*WpViewporter = null,
    subcompositor: ?*WlSubcompositor = null,
    data_device_manager: ?*WlDataDeviceManager = null,
};

pub const NwlState = extern struct {
    wl: NwlStateWlObjects = .{},
    seats: WlListHead(NwlSeat, .link) = .{},
    outputs: WlListHead(NwlOutput, .link) = .{},
    surfaces: WlListHead(NwlSurface, .link) = .{},
    surfaces_dirty: WlListHead(NwlSurface, .dirtlink) = .{},
    globals: WlListHead(NwlGlobal, .link) = .{},
    subs: WlListHead(NwlStateSub, .link) = .{},
    cursor_theme: ?*WlCursorTheme = null,
    cursor_theme_size: u32 = 0,
    num_surfaces: u32 = 0,
    run_with_zero_surfaces: bool = false,
    poll: ?*NwlPoll = null,
    events: NwlStateEventsImpl = .{},
    xdg_app_id: ?[*:0]const u8 = null,
    extern fn nwl_wayland_init(state: *NwlState) u8;
    extern fn nwl_wayland_uninit(state: *NwlState) void;
    extern fn nwl_wayland_run(state: *NwlState) void;
    extern fn nwl_surface_create(state: *NwlState, title: [*:0]const u8) ?*NwlSurface;
    extern fn nwl_state_add_sub(state: *NwlState, subimpl: *NwlStateSubImpl, data: ?*anyopaque) void;
    extern fn nwl_state_get_sub(state: *NwlState, subimpl: *NwlStateSubImpl) ?*anyopaque;
    extern fn nwl_poll_add_fd(state: *NwlState, fd: c_int, callback: nwl_poll_callback_t, data: ?*anyopaque) void;
    extern fn nwl_poll_del_fd(state: *NwlState, fd: c_int) void;
    extern fn nwl_poll_get_fd(state: *NwlState) std.os.fd_t;
    extern fn nwl_poll_dispatch(state: *NwlState, timeout: c_int) bool;

    pub const addSub = nwl_state_add_sub;
    pub const getSub = nwl_state_get_sub;
    pub const addFd = nwl_poll_add_fd;
    pub const delFd = nwl_poll_del_fd;
    pub const getFd = nwl_poll_get_fd;
    pub const dispatch = nwl_poll_dispatch;
    pub fn waylandInit(self: *NwlState) NwlError!void {
        if (self.nwl_wayland_init() != 0) {
            return NwlError.InitFailed;
        }
    }
    pub const waylandUninit = nwl_wayland_uninit;
    pub const run = nwl_wayland_run;
    pub fn createSurface(self: *NwlState, title: [*:0]const u8) NwlError!*NwlSurface {
        return nwl_surface_create(self, title) orelse NwlError.SurfaceCreateFailed;
    }
};


pub const ShmPool = extern struct {
    fd:std.os.fd_t = -1,
    data:[*]u8 = undefined,
    pool:?*WlShmPool = null,
    size:usize = 0,
};

pub const ShmBuffer = extern struct {
    pub const BufferFlags = packed struct(u8) {
        acquired:bool = false,
        destroy:bool = false,
        _pad:u6 = 0
    };
    wl_buffer:?*WlBuffer = null,
    bufferdata:[*]u8 = undefined,
    data:?*anyopaque = null,
    flags:BufferFlags = .{},

    pub fn release(self:*ShmBuffer) void {
        self.flags.acquired = false;
    }
};

pub const ShmBufferMan = extern struct {
    pub const RendererImpl = extern struct {
        buffer_create:*const fn(buffer:*ShmBuffer, bufferman:*ShmBufferMan) callconv(.C) void,
        buffer_destroy:*const fn(buffer:*ShmBuffer, bufferman:*ShmBufferMan) callconv(.C) void,
    };
    pool:ShmPool = .{},
    buffers:[4]ShmBuffer = [_]ShmBuffer{.{}} ** 4,
    impl:?*const RendererImpl = null,
    width:u32 = 0,
    height:u32 = 0,
    stride:u32 = 0,
    format:u32 = 0,
    num_slots:u8 = 1,

    extern fn nwl_shm_bufferman_get_next(bufferman:*ShmBufferMan) ?*ShmBuffer;
    pub const getNext = nwl_shm_bufferman_get_next;
    extern fn nwl_shm_bufferman_set_slots(bufferman:*ShmBufferMan, state:*NwlState, num_slots:u8) void;
    pub const setSlots = nwl_shm_bufferman_set_slots;
    extern fn nwl_shm_bufferman_resize(bufferman:*ShmBufferMan, state:*NwlState, width:u32, height:u32, stride:u32, format:u32) void;
    pub const resize = nwl_shm_bufferman_resize;
};
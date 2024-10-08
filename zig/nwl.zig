//! nwl, for Zig!
//! Top tip! In root do something like...
//! pub const wayland = @import("wayland");
//! Then Wayland types here will become zig-wayland types!

const std = @import("std");

pub const Global = extern struct {
    const Impl = extern struct {
        destroy: ?*const fn (?*anyopaque) callconv(.C) void,
    };
    link: WlList,
    name: u32,
    global: ?*anyopaque,
    impl: Impl,
};

/// CamelCasify a Wayland object name, to match zig-wayland
const WlObjectNamer = struct {
    fn doWrite(name: []const u8, writer: anytype) !void {
        var tok = std.mem.tokenizeScalar(u8, name, '_');
        while (tok.next()) |t| {
            try writer.writeByte(std.ascii.toUpper(t[0]));
            try writer.writeAll(t[1..]);
        }
    }
    pub fn casify(comptime name: []const u8) []const u8 {
        var cw = std.io.countingWriter(std.io.null_writer);
        WlObjectNamer.doWrite(name, cw.writer()) catch unreachable;
        var buf: [cw.bytes_written]u8 = undefined;
        var stream = std.io.fixedBufferStream(buf[0..]);
        WlObjectNamer.doWrite(name, stream.writer()) catch unreachable;
        return buf[0..];
    }
};

/// Make a Wayland type, either a opaque or zig-wayland type
fn WaylandObject(comptime name: []const u8) type {
    const root = @import("root");
    if (@hasDecl(root, "wayland")) {
        const client = root.wayland.client;
        const delim = std.mem.indexOf(u8, name, "_");
        if (delim) |d| {
            const prefix = name[0..d];
            if (@hasDecl(client, prefix)) {
                const sub = @field(client, prefix);
                const camelname = WlObjectNamer.casify(name[d + 1 ..]);
                if (@hasDecl(sub, camelname)) {
                    return @field(sub, camelname);
                }
            }
        } else if (@hasDecl(client, name)) {
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
pub const XdgPositioner = WaylandObject("xdg_positioner");

pub const XdgToplevel = WaylandObject("xdg_toplevel");
pub const XdgPopup = WaylandObject("xdg_popup");
pub const ZwlrLayerSurfaceV1 = WaylandObject("zwlr_layer_surface_v1");
pub const WpCursorShapeDeviceV1 = WaylandObject("wp_cursor_shape_device_v1");
pub const WpCursorShapeManagerV1 = WaylandObject("wp_cursor_shape_manager_v1");
pub const XkbContext = opaque {};
pub const WlCursorTheme = opaque {};
pub const Poll = opaque {};

pub const Output = extern struct {
    state: *State,
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
pub const StateSubImpl = extern struct {
    destroy: ?*const fn (*StateSub) callconv(.C) void,
};
pub const StateSub = extern struct {
    link: WlList,
    impl: *StateSubImpl,
};

// TODO: figure out if it's possible to make this typed.
pub const WlList = extern struct {
    prev: *WlList = undefined,
    next: *WlList = undefined,
};

pub fn WlListHead(comptime linktype: type, comptime field: std.meta.FieldEnum(linktype)) type {
    const tinfo = @typeInfo(linktype);
    if (tinfo != .@"struct") {
        @compileError("expected struct, got " ++ .{@typeName(linktype)});
    }
    const fieldinfo = std.meta.fieldInfo(linktype, field);
    if (fieldinfo.type != WlList) {
        @compileError(fieldinfo.name ++ " is a " ++ @typeName(fieldinfo.type) ++ ", not a WlList!");
    }
    return extern struct {
        prev: *WlList = undefined,
        next: *WlList = undefined,
        const Iterator = struct {
            this: *WlList,
            pos: *WlList,

            pub fn next(it: *Iterator) ?*linktype {
                if (it.this != it.pos) {
                    const ret = it.pos;
                    it.pos = it.pos.next;
                    return @fieldParentPtr(fieldinfo.name, ret);
                }
                return null;
            }
        };
        pub fn iterator(self: *@This()) Iterator {
            return .{ .this = @ptrCast(self), .pos = self.next };
        }
    };
}

fn WlArray(comptime T: type) type {
    return extern struct {
        size: usize,
        alloc: usize,
        data: ?[*]T,

        pub fn slice(self: @This()) []T {
            return self.data.?[0 .. self.size / @sizeOf(T)];
        }
    };
}

pub const WlFixed = enum(i32) {
    _,
    pub fn toDouble(f: WlFixed) f64 {
        const i = @as(i64, ((1023 + 44) << 52) + (1 << 51)) + @intFromEnum(f);
        return @as(f64, @bitCast(i)) - (3 << 43);
    }
    pub fn fromDouble(d: f64) WlFixed {
        const i = d + (3 << (51 - 8));
        return @enumFromInt(@as(i64, @bitCast(i)));
    }
    pub fn toInt(self: WlFixed) c_int {
        return @divTrunc(@intFromEnum(self), @as(c_int, 256));
    }
    pub fn fromInt(arg_i: c_int) WlFixed {
        return arg_i * @as(c_int, 256);
    }

    pub fn format(value: WlFixed, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        try std.fmt.formatType(value.toDouble(), fmt, options, writer, 1);
    }
};

const DataDevice = extern struct {
    wl: ?*WlDataDevice,
    drop: DataOffer,
    selection: DataOffer,
    incoming: DataOffer,
    event: DndEvent,
};
pub const KeyboardEvent = extern struct {
    const EventType = enum(u8) {
        focus,
        keydown,
        keyup,
        keyrepeat,
        modifiers,
    };
    const XkbKeysym = u32;
    const XkbKeycode = u32;
    const ComposeState = enum(u8) { none, composing, composed };
    type: EventType,
    compose_state: ComposeState,
    focus: bool,
    keysym: XkbKeysym,
    keycode: XkbKeycode,
    utf8: [16]u8,
    serial: u32,
};

pub const PointerEvent = extern struct {
    const Changed = packed struct(u8) {
        focus: bool,
        button: bool,
        motion: bool,
        axis: bool,
        _padding: u4,
    };
    const Buttons = packed struct(u8) {
        left: bool,
        middle: bool,
        right: bool,
        back: bool,
        forward: bool,
        _padding: u3,
    };
    const AxisSource = packed struct(u8) {
        wheel: bool,
        finger: bool,
        continuous: bool,
        wheel_tilt: bool,
        _pad: u4,
    };
    const Axis = packed struct(u8) {
        vertical: bool,
        horizontal: bool,
        _pad: u6,
    };
    changed: Changed,
    serial: u32,
    surface_x: WlFixed,
    surface_y: WlFixed,
    buttons: Buttons,
    buttons_prev: Buttons,
    axis_value_vert: i32,
    axis_value_hori: i32,
    axis_hori: WlFixed,
    axis_vert: WlFixed,
    axis_source: AxisSource,
    axis_stop: Axis,
    focus: bool,
};

const PointerSurface = extern struct {
    shape_device: ?*WpCursorShapeDeviceV1,
    xcursor_surface: ?*WlSurface,
    nwl: ?*Surface,
};

pub const Seat = extern struct {
    const KeymapXkb = extern struct {
        const XkbKeymap = opaque {};
        const XkbState = opaque {};
        const XkbComposeState = opaque {};
        const XkbComposeTable = opaque {};

        keyboard_keymap: ?*XkbKeymap,
        keyboard_state: ?*XkbState,
        keyboard_compose_state: ?*XkbComposeState,
        keyboard_compose_table: ?*XkbComposeTable,
    };
    const CursorShape = enum(u32) {
        none = 0,
        default,
        context_menu,
        help,
        pointer,
        progress,
        wait,
        cell,
        crosshair,
        text,
        vertical_text,
        alias,
        copy,
        move,
        no_drop,
        not_allowed,
        grab,
        grabbing,
        e_resize,
        n_resize,
        ne_resize,
        nw_resize,
        s_resize,
        se_resize,
        sw_resize,
        w_resize,
        ew_resize,
        ns_resize,
        nesw_resize,
        nwse_resize,
        col_resize,
        row_resize,
        all_scroll,
        zoom_in,
        zoom_out,
    };

    state: *State,
    link: WlList,
    wl_seat: *WlSeat,
    data_device: DataDevice,
    keyboard: ?*WlKeyboard,
    keyboard_xkb: KeymapXkb,
    keyboard_compose_enabled: bool,
    keyboard_repeat_enabled: bool,
    keyboard_focus: ?*Surface,
    keyboard_repeat_rate: i32,
    keyboard_repeat_delay: i32,
    keyboard_repeat_fd: c_int,
    keyboard_event: ?*KeyboardEvent,
    touch: ?*WlTouch,
    touch_focus: ?*Surface,
    touch_serial: u32,
    pointer: ?*WlPointer,
    pointer_focus: ?*Surface,
    pointer_prev_focus: ?*Surface,
    pointer_surface: PointerSurface,
    pointer_event: ?*PointerEvent,
    name: ?[*:0]u8,
    userdata: ?*anyopaque,

    extern fn nwl_seat_set_pointer_cursor(seat: *Seat, cursor: [*:0]const u8) void;
    extern fn nwl_seat_set_pointer_shape(seat: *Seat, shape: CursorShape) void;
    extern fn nwl_seat_set_pointer_surface(seat: *Seat, surface: *Surface, hotspot_x: i32, hotspot_y: i32) bool;
    extern fn nwl_seat_start_drag(seat: *Seat, data_source: *WlDataSource, icon: ?*Surface) void;

    pub const setPointerCursor = nwl_seat_set_pointer_cursor;
    pub const setPointerShape = nwl_seat_set_pointer_shape;
    pub fn setPointerSurface(seat: *Seat, surface: *Surface, hotspot_x: i32, hotspot_y: i32) !void {
        if (!seat.nwl_seat_set_pointer_surface(surface, hotspot_x, hotspot_y)) {
            //TODO: improve this
            return error.Generic;
        }
    }
    pub fn startDrag(seat: *Seat, data_source: *WlDataSource, icon: ?*Surface) !void {
        if (!seat.nwl_seat_start_drag(data_source, icon)) {
            //TODO: improve this
            return error.Generic;
        }
    }
};

pub const DndEvent = extern struct {
    const EventType = enum(u8) { motion = 0, enter, left, drop };
    type: EventType,
    serial: u32,
    focus_surface: ?*Surface,
    x: WlFixed,
    y: WlFixed,
    time: u32,
    source_actions: u32,
    action: u32,
};

pub const DataOffer = extern struct {
    mime: WlArray([*:0]u8),
    offer: ?*WlDataOffer,
};

pub const Cairo = struct {
    pub const CairoSurface = extern struct {
        const cairo_surface_t = opaque {};
        const cairo_t = opaque {};
        ctx: *cairo_t,
        surface: *cairo_surface_t,
        rerender: bool,
    };
    pub const Renderer = extern struct {
        shm: ShmBufferMan,
        cairo_surfaces: [ShmBufferMan.max_buffers]CairoSurface,
        next_buffer: c_int,
        prev_buffer: c_int,

        extern fn nwl_cairo_renderer_init(renderer: *Renderer) void;
        pub const init = nwl_cairo_renderer_init;
        extern fn nwl_cairo_renderer_finish(renderer: *Renderer) void;
        pub const deinit = nwl_cairo_renderer_finish;
        extern fn nwl_cairo_renderer_submit(renderer: *Renderer, surface: *Surface, x: i32, y: i32) void;
        pub const submit = nwl_cairo_renderer_submit;
        extern fn nwl_cairo_renderer_get_surface(renderer: *Renderer, surface: *Surface, copyprevious: bool) ?*CairoSurface;
        pub const getSurface = nwl_cairo_renderer_get_surface;
    };
};

extern fn wl_proxy_marshal(p: ?*WlProxy, opcode: u32, ...) void;

const Error = error{ InitFailed, RoleSetFailed, SurfaceCreateFailed };

pub const Surface = extern struct {
    const GenericSurfaceFn = *const fn (*Surface) callconv(.C) void;
    const RoleId = enum(u8) { none, toplevel, popup, layer, sub, cursor, dragicon };
    const Flags = packed struct(u32) {
        no_autoscale: bool = false,
        no_autocursor: bool = false,
        padding: u30 = 0,
    };

    const SurfaceStates = packed struct(u32) {
        active: bool,
        maximized: bool,
        fullscreen: bool,
        resizing: bool,
        tiled_left: bool,
        tiled_right: bool,
        tiled_top: bool,
        tiled_bottom: bool,
        csd: bool,
        needs_update: bool,
        needs_applysize: bool,
        destroy: bool,
        needs_configure: bool,
        _padding: u19,
    };

    const SurfaceImpl = extern struct {
        update: ?GenericSurfaceFn = null,
        destroy: ?GenericSurfaceFn = null,
        input_pointer: ?*const fn (*Surface, *Seat, *PointerEvent) callconv(.C) void = null,
        input_keyboard: ?*const fn (*Surface, *Seat, *KeyboardEvent) callconv(.C) void = null,
        dnd: ?*const fn (*Surface, *Seat, *DndEvent) callconv(.C) void = null,
        configure: ?*const fn (*Surface, u32, u32) callconv(.C) void = null,
        close: ?GenericSurfaceFn = null,
    };
    const RoleUnion = extern union {
        toplevel: extern struct {
            const Capabilities = packed struct(u8) {
                window_menu: bool,
                maximize: bool,
                fullscreen: bool,
                minimize: bool,
                _pad: u4,
            };
            wl: *XdgToplevel,
            decoration: ?*ZxdgToplevelDecorationV1,
            wm_capabilities: Capabilities,
            bounds_width: i32,
            bounds_height: i32,
        },
        popup: extern struct {
            wl: *XdgPopup,
            lx: i32,
            ly: i32,
            reposition_token: u32,
        },
        layer: extern struct { wl: *ZwlrLayerSurfaceV1 },
        subsurface: extern struct {
            wl: *WlSubsurface,
            parent: *Surface,
        },
    };

    link: WlList = .{},
    dirtlink: WlList = .{},
    state: *State = undefined,
    wl: extern struct {
        surface: *WlSurface,
        xdg_surface: ?*XdgSurface,
        frame_cb: ?*WlCallback,
    } = undefined,
    width: u32 = undefined,
    height: u32 = undefined,
    desired_width: u32 = 640,
    desired_height: u32 = 480,
    current_width: u32 = undefined,
    current_height: u32 = undefined,
    configure_serial: u32 = undefined,
    scale: i32 = undefined,
    scale_preferred: i32 = undefined,
    subsurfaces: WlList = .{},
    outputs: extern struct {
        outputs: [*]*Output,
        amount: u32,
    } = undefined,
    flags: Flags = .{},
    states: SurfaceStates = undefined,
    title: ?[*:0]u8 = null,
    role_id: RoleId = undefined,
    defer_update: bool = undefined,
    role: RoleUnion = undefined,
    frame: u32 = 0,
    impl: SurfaceImpl = .{},
    extern fn nwl_surface_destroy(surface: *Surface) void;
    extern fn nwl_surface_destroy_later(surface: *Surface) void;
    extern fn nwl_surface_set_size(surface: *Surface, width: u32, height: u32) void;
    extern fn nwl_surface_set_title(surface: *Surface, title: ?[*:0]const u8) void;
    extern fn nwl_surface_update(surface: *Surface) void;
    extern fn nwl_surface_set_need_update(surface: *Surface, now: bool) void;
    extern fn nwl_surface_role_subsurface(surface: *Surface, parent: *Surface) bool;
    extern fn nwl_surface_role_layershell(surface: *Surface, output: ?*WlOutput, layer: u32) bool;
    extern fn nwl_surface_role_toplevel(surface: *Surface) bool;
    extern fn nwl_surface_role_popup(surface: *Surface, parent: *Surface, positioner: *XdgPositioner) bool;
    extern fn nwl_surface_role_unset(surface: *Surface) void;
    extern fn nwl_surface_init(surface: *Surface, state: *State, title: [*:0]const u8) void;
    extern fn nwl_surface_buffer_submitted(surface: *Surface) void;
    pub fn commit(self: *Surface) void {
        if (@hasDecl(WlSurface, "commit")) {
            self.wl.surface.commit();
        } else {
            wl_proxy_marshal(@ptrCast(self.wl.surface), @as(u32, 6));
        }
    }
    pub fn roleTopLevel(self: *Surface) Error!void {
        if (!nwl_surface_role_toplevel(self)) {
            return Error.RoleSetFailed;
        }
    }
    pub fn rolePopup(self: *Surface, parent: *Surface, positioner: *XdgPositioner) Error!void {
        if (!nwl_surface_role_popup(self, parent, positioner)) {
            return Error.RoleSetFailed;
        }
    }
    pub fn roleLayershell(self: *Surface, output: ?*WlOutput, layer: u32) Error!void {
        if (!nwl_surface_role_layershell(self, output, layer)) {
            return Error.RoleSetFailed;
        }
    }
    pub fn roleSubsurface(self: *Surface, parent: *Surface) Error!void {
        if (!nwl_surface_role_subsurface(self, parent)) {
            return Error.RoleSetFailed;
        }
    }
    pub const setSize = nwl_surface_set_size;
    pub const update = nwl_surface_update;
    pub const setTitle = nwl_surface_set_title;
    pub const setNeedUpdate = nwl_surface_set_need_update;
    pub const bufferSubmitted = nwl_surface_buffer_submitted;
    pub const destroy = nwl_surface_destroy;
    pub const destroyLater = nwl_surface_destroy_later;
    pub const unsetRole = nwl_surface_role_unset;
    pub const init = nwl_surface_init;
};

pub const State = extern struct {
    pub const PollCallbackFn = *const fn (*State, u32, ?*anyopaque) callconv(.C) void;
    pub const BoundGlobal = extern struct {
        pub const Kind = enum(c_int) { output = 0, seat };
        kind:Kind,
        global:extern union{
            output:*Output,
            seat:*Seat,
        }
    };
    wl: extern struct {
        display: ?*WlDisplay = null,
        registry: ?*WlRegistry = null,
        compositor: ?*WlCompositor = null,
        shm: ?*WlShm = null,
        xdg_wm_base: ?*XdgWmBase = null,
        xdg_output_manager: ?*ZxdgOutputManagerV1 = null,
        layer_shell: ?*ZwlrLayerShellV1 = null,
        decoration: ?*ZxdgDecorationManagerV1 = null,
        subcompositor: ?*WlSubcompositor = null,
        data_device_manager: ?*WlDataDeviceManager = null,
        cursor_shape_manager: ?*WpCursorShapeManagerV1 = null,
    } = .{},
    seats: WlListHead(Seat, .link) = .{},
    outputs: WlListHead(Output, .link) = .{},
    surfaces: WlListHead(Surface, .link) = .{},
    surfaces_dirty: WlListHead(Surface, .dirtlink) = .{},
    globals: WlListHead(Global, .link) = .{},
    subs: WlListHead(StateSub, .link) = .{},
    cursor_theme: ?*WlCursorTheme = null,
    cursor_theme_size: u32 = 0,
    num_surfaces: u32 = 0,
    run_with_zero_surfaces: bool = false,
    poll: ?*Poll = null,
    events: extern struct {
        global_bound: ?*const fn (global: *const BoundGlobal) callconv(.C) void = null,
        global_destroy: ?*const fn (global: *const BoundGlobal) callconv(.C) void = null,
        global_add: ?*const fn (*State, *WlRegistry, u32, [*:0]const u8, u32) callconv(.C) bool = null,
        global_remove: ?*const fn (*State, *WlRegistry, u32) callconv(.C) void = null,
    } = .{},
    xdg_app_id: ?[*:0]const u8 = null,
    extern fn nwl_wayland_init(state: *State) u8;
    extern fn nwl_wayland_uninit(state: *State) void;
    extern fn nwl_wayland_run(state: *State) void;
    extern fn nwl_state_add_sub(state: *State, sub: *StateSub) void;
    extern fn nwl_state_get_sub(state: *State, impl: *StateSubImpl) ?*StateSub;
    extern fn nwl_poll_add_fd(state: *State, fd: c_int, events: u32, callback: PollCallbackFn, data: ?*anyopaque) void;
    extern fn nwl_poll_del_fd(state: *State, fd: c_int) void;
    extern fn nwl_poll_get_fd(state: *State) std.posix.fd_t;
    extern fn nwl_poll_dispatch(state: *State, timeout: c_int) bool;

    pub const addSub = nwl_state_add_sub;
    pub const getSub = nwl_state_get_sub;
    pub const addFd = nwl_poll_add_fd;
    pub const delFd = nwl_poll_del_fd;
    pub const getFd = nwl_poll_get_fd;
    pub const dispatch = nwl_poll_dispatch;
    pub fn waylandInit(self: *State) Error!void {
        if (self.nwl_wayland_init() != 0) {
            return Error.InitFailed;
        }
    }
    pub const waylandUninit = nwl_wayland_uninit;
    pub const run = nwl_wayland_run;
};

pub const ShmPool = extern struct {
    fd: std.posix.fd_t = -1,
    data: [*]u8 = undefined,
    pool: ?*WlShmPool = null,
    size: usize = 0,

    extern fn nwl_shm_pool_finish(pool: *ShmPool) void;
    pub const finish = nwl_shm_pool_finish;
};

pub const ShmBufferMan = extern struct {
    pub const max_buffers = 4;
    pub const Buffer = extern struct {
        const Flags = packed struct(u8) {
            acquired: bool = false,
            destroy: bool = false,
            _pad: u6 = 0,
        };
        wl_buffer: ?*WlBuffer = null,
        bufferdata: [*]u8 = undefined,
        flags: Flags = .{},
    };
    pub const RendererImpl = extern struct {
        buffer_create: *const fn (buf_idx: c_uint, bufferman: *ShmBufferMan) callconv(.C) void,
        buffer_destroy: *const fn (buf_idx: c_uint, bufferman: *ShmBufferMan) callconv(.C) void,
    };
    pool: ShmPool = .{},
    buffers: [max_buffers]Buffer = [_]Buffer{.{}} ** max_buffers,
    impl: ?*const RendererImpl = null,
    width: u32 = 0,
    height: u32 = 0,
    stride: u32 = 0,
    format: u32 = 0,
    num_slots: u8 = 1,

    extern fn nwl_shm_bufferman_get_next(bufferman: *ShmBufferMan) c_int;
    pub fn getNext(bufferman: *ShmBufferMan) !c_uint {
        const ret = bufferman.nwl_shm_bufferman_get_next();
        if (ret != -1) {
            return @intCast(ret);
        }
        return error.NoAvailableBuffer;
    }
    extern fn nwl_shm_bufferman_set_slots(bufferman: *ShmBufferMan, wl_shm: *WlShm, num_slots: u8) void;
    pub const setSlots = nwl_shm_bufferman_set_slots;
    extern fn nwl_shm_bufferman_resize(bufferman: *ShmBufferMan, wl_shm: *WlShm, width: u32, height: u32, stride: u32, format: u32) void;
    pub const resize = nwl_shm_bufferman_resize;
    extern fn nwl_shm_bufferman_finish(bufferman: *ShmBufferMan) void;
    pub const finish = nwl_shm_bufferman_finish;
};

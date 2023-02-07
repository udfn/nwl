const std = @import("std");

const WlScannerStep = struct {
    step: std.Build.Step,
    builder: *std.Build,
    queue: std.SinglyLinkedList([]const u8),
    system_queue: std.SinglyLinkedList([]const u8),
    // Figure out whether it's smarter to inject the C files directly into libs like this..
    // or whether to compile a separate lib and then link that..
    dest_libs: std.SinglyLinkedList(*std.Build.CompileStep),
    dest_path: []const u8,
    gen_server_headers: bool,
    client_header_suffix: []const u8,
    const Self = @This();
    pub fn create(b: *std.Build) !*Self {
        const res = try b.allocator.create(Self);
        const dest_path = try std.fs.path.join(b.allocator, &.{ b.cache_root.path.?, "wl-gen" });
        res.* = .{
            .step = std.Build.Step.init(.custom, "wl-scanner", b.allocator, make),
            .queue = .{},
            .system_queue = .{},
            .dest_libs = .{},
            .builder = b,
            .dest_path = dest_path,
            .gen_server_headers = false,
            .client_header_suffix = "-client-protocol.h",
        };
        return res;
    }
    pub fn linkWith(self: *Self, lib: *std.Build.CompileStep) void {
        const node = self.builder.allocator.create(std.SinglyLinkedList(*std.Build.CompileStep).Node) catch @panic("OOM");
        node.data = lib;
        lib.step.dependOn(&self.step);
        lib.addIncludePath(self.dest_path);
        self.dest_libs.prepend(node);
    }
    pub fn addProtocol(self: *Self, xml: []const u8) void {
        const node = self.builder.allocator.create(std.SinglyLinkedList([]const u8).Node) catch @panic("OOM");
        node.data = self.builder.dupe(xml);
        self.queue.prepend(node);
    }
    pub fn addProtocolFromPath(self: *Self, base: []const u8, xml: []const u8) void {
        var pathbuf: [1024]u8 = undefined;
        var ally = std.heap.FixedBufferAllocator.init(pathbuf[0..]);
        self.addProtocol(std.fs.path.join(ally.allocator(), &.{ base, xml }) catch @panic("This wasn't supposed to happen..."));
    }
    pub fn addSystemProtocols(self: *Self, xmls: []const []const u8) void {
        for (xmls) |x| {
            const node = self.builder.allocator.create(std.SinglyLinkedList([]const u8).Node) catch @panic("OOM");
            node.data = x;
            self.system_queue.prepend(node);
        }
    }
    pub fn addProtocolsFromPath(self: *Self, base: []const u8, xmls: []const []const u8) void {
        for (xmls) |x| {
            self.addProtocolFromPath(base, x);
        }
    }

    const WlScanGenType = enum {
        code,
        clientheader,
        serverheader,
        pub fn toString(self: WlScanGenType) []const u8 {
            switch (self) {
                .code => return "private-code",
                .clientheader => return "client-header",
                .serverheader => return "server-header",
            }
        }
    };

    const WlScanError = error{WaylandScannerFail};

    fn runScanner(self: *Self, protocol: []const u8, gentype: WlScanGenType, output: []const u8) !void {
        var child = std.ChildProcess.init(&.{ "wayland-scanner", gentype.toString(), protocol, output }, self.builder.allocator);
        child.env_map = self.builder.env_map;
        child.spawn() catch |err| {
            std.log.err("Unable to spawn wayland-scanner: {s}", .{@errorName(err)});
            return WlScanError.WaylandScannerFail;
        };
        _ = try child.wait();
    }

    fn processProtocol(self: *Self, ally: std.mem.Allocator, protocol: []const u8, gentype: WlScanGenType, destdir: *const std.fs.Dir, realdestpath: []const u8) !void {
        const protoname = std.fs.path.stem(protocol);
        const filesuffix = switch (gentype) {
            .code => ".c",
            .clientheader => self.client_header_suffix,
            .serverheader => "-protocol.h",
        };
        const filename = try std.mem.concat(ally, u8, &.{ protoname, filesuffix });
        var needscanner = brk: {
            const outstat = destdir.statFile(filename) catch break :brk true;
            const instat = std.fs.cwd().statFile(protocol) catch unreachable;
            break :brk outstat.mtime < instat.mtime;
        };
        if (needscanner) {
            var fullpath = try std.fs.path.join(ally, &.{ realdestpath, filename });
            try self.runScanner(protocol, gentype, fullpath);
        }
        if (gentype == .code) {
            var it = self.dest_libs.first;
            if (it == null) {
                return;
            }
            const path = try std.fs.path.join(ally, &.{ self.dest_path, filename });
            while (it) |node| : (it = node.next) {
                node.data.addCSourceFile(path, &.{});
            }
        }
    }

    pub fn process(self:*Self, protocol:[]const u8, dest: *const std.fs.Dir, realdestpath: []const u8) !void {
        var buf: [2048]u8 = undefined;
        var fba = std.heap.FixedBufferAllocator.init(buf[0..]);
        var ally = fba.allocator();
        try self.processProtocol(ally, protocol, .code, dest, realdestpath);
        fba.reset();
        try self.processProtocol(ally, protocol, .clientheader, dest, realdestpath);
        if (self.gen_server_headers) {
            fba.reset();
            try self.processProtocol(ally, protocol, .serverheader, dest, realdestpath);
        }
    }

    pub fn make(step: *std.Build.Step) !void {
        const self = @fieldParentPtr(Self, "step", step);
        var it = self.queue.first;
        if (self.queue.first == null and self.system_queue.first == null) {
            return;
        }
        const dest = try std.fs.cwd().makeOpenPath(self.builder.pathFromRoot(self.dest_path), .{});
        const realdestpath = try dest.realpathAlloc(self.builder.allocator, ".");
        while (it) |node| : (it = node.next) {
            try self.process(node.data, &dest, realdestpath);
        }
        it = self.system_queue.first;
        if (it != null) {
            const sysprotopath = try self.getSysWlProtocolsDir();
            var buf: [2048]u8 = undefined;
            var fba = std.heap.FixedBufferAllocator.init(&buf);
            while (it) |node| : (it = node.next) {
                const newpath = try std.fs.path.join(fba.allocator(), &.{sysprotopath, node.data});
                try self.process(newpath, &dest, realdestpath);
                fba.reset();
            }
        }
    }
    fn getSysWlProtocolsDir(self: *Self) ![]const u8 {
        // Don't really like doing this, but you gotta do what you gotta do..
        const systemprotodir = try self.builder.exec(&.{ "pkg-config", "--variable=pkgdatadir", "wayland-protocols" });
        return std.mem.trim(u8, systemprotodir, &std.ascii.whitespace);
    }
};

pub fn build(b: *std.build.Builder) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const nwl = b.addStaticLibrary(.{
        .name = "nwl",
        .target = target,
        .optimize = optimize
    });
    _ = b.addModule("nwl", .{
        .source_file = .{ .path = "zig/nwl.zig"}
    });
    // Maybe have an OptionsStep as well so features are reflected in nwl.zig?
    const egl = b.option(bool, "egl", "EGL support") orelse false;
    const cairo = b.option(bool, "cairo", "Cairo renderer") orelse false;
    const seat = b.option(bool, "seat", "Wayland seat support") orelse false;
    nwl.linkLibC();
    nwl.linkSystemLibrary("wayland-client");
    nwl.addIncludePath(".");
    nwl.addCSourceFiles(&.{
        "src/shell.c",
        "src/shm.c",
        "src/surface.c",
        "src/wayland.c" }, &.{});
    if (seat) {
        nwl.linkSystemLibrary("xkbcommon");
        nwl.linkSystemLibrary("wayland-cursor");
        nwl.addCSourceFile("src/seat.c", &.{});
    }
    if (egl) {
        nwl.addCSourceFile("src/egl.c", &.{});
        nwl.linkSystemLibrary("wayland-egl");
        nwl.linkSystemLibrary("epoxy");
    }
    if (cairo) {
        nwl.addCSourceFile("src/cairo.c", &.{});
        nwl.linkSystemLibrary("cairo");
    }
    const conf = b.addConfigHeader(.{.include_path = "nwl/config.h"}, .{
        .NWL_HAS_SEAT = seat
    });
    nwl.addConfigHeader(conf);
    const scannerstep = try WlScannerStep.create(b);
    scannerstep.client_header_suffix = ".h";
    scannerstep.linkWith(nwl);
    scannerstep.addSystemProtocols(&.{
        "stable/viewporter/viewporter.xml",
        "stable/xdg-shell/xdg-shell.xml",
        "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml",
        "unstable/xdg-output/xdg-output-unstable-v1.xml"
    });
    scannerstep.addProtocol(b.pathFromRoot("protocol/wlr-layer-shell-unstable-v1.xml"));
    nwl.install();
    const headerinstall = b.addInstallDirectory(.{ .source_dir = "nwl", .install_dir = .header, .install_subdir = "nwl", .exclude_extensions = &.{"build"} });
    headerinstall.step.dependOn(&nwl.step);
    b.getInstallStep().dependOn(&headerinstall.step);
}

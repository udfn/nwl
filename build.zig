const std = @import("std");
const WlScannerStep = @import("wlscannerstep").WlScannerStep;

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const nwl = b.addStaticLibrary(.{ .name = "nwl_lib", .link_libc = true, .target = target, .optimize = optimize,});
    const mod = b.addModule("nwl", .{.root_source_file = .{ .path = "zig/nwl.zig" }});
    mod.linkLibrary(nwl);
    // Maybe have an OptionsStep as well so features are reflected in nwl.zig?
    const egl = b.option(bool, "egl", "EGL support") orelse false;
    const cairo = b.option(bool, "cairo", "Cairo renderer") orelse false;
    const seat = b.option(bool, "seat", "Wayland seat support") orelse false;
    const scannerstep = try WlScannerStep.create(b, .{
        .target = target,
        .optimize = optimize,
        .client_header_suffix = ".h",
    });
    scannerstep.linkWith(nwl);
    scannerstep.addSystemProtocols(&.{
        "stable/xdg-shell/xdg-shell.xml",
        "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml",
        "unstable/xdg-output/xdg-output-unstable-v1.xml",
    });
    scannerstep.addProtocol(b.pathFromRoot("protocol/wlr-layer-shell-unstable-v1.xml"), false);
    nwl.addIncludePath(.{.path = "."});
    nwl.addCSourceFiles(.{ .files = &.{
        "src/shell.c",
        "src/shm.c",
        "src/surface.c",
        "src/wayland.c",
    }});
    if (seat) {
        nwl.linkSystemLibrary("xkbcommon");
        nwl.linkSystemLibrary("wayland-cursor");
        nwl.addCSourceFile(.{.file = .{.path = "src/seat.c"}, .flags = &.{}});
        scannerstep.addSystemProtocols(&.{
            "staging/cursor-shape/cursor-shape-v1.xml",
            "unstable/tablet/tablet-unstable-v2.xml",
        });
    }
    if (egl) {
        nwl.addCSourceFile(.{.file = .{.path = "src/egl.c"}, .flags = &.{}});
        nwl.linkSystemLibrary("wayland-egl");
        nwl.linkSystemLibrary("epoxy");
    }
    if (cairo) {
        nwl.addCSourceFile(.{.file = .{.path = "src/cairo.c"}, .flags = &.{}});
        nwl.linkSystemLibrary("cairo");
    }
    const conf = b.addConfigHeader(.{ .include_path = "nwl/config.h" }, .{
        .NWL_HAS_SEAT = @intFromBool(seat),
    });
    nwl.addConfigHeader(conf);
    b.installArtifact(nwl);
    nwl.installHeadersDirectoryOptions(.{
        .source_dir = .{ .path = "nwl" },
        .install_dir = .header,
        .install_subdir = "nwl",
        .exclude_extensions = &.{"build"},
    });
}

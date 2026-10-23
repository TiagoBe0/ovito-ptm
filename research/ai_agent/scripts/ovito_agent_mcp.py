#!/usr/bin/env python3
"""MCP server exposing a running OVITO session to an AI coding agent.

This is the counterpart of the AgentBridgeServer built into this OVITO fork
(src/ovito/aiagent/). OVITO Pro solves the same problem by running a Jupyter kernel
inside its embedded Python interpreter; this build has no embedded interpreter, so the
bridge speaks a small JSON-RPC dialect over a local socket instead and this script
translates between that dialect and the Model Context Protocol.

It deliberately has no third-party dependencies: the agent's Python environment is not
under our control, and asking the user to pip-install into it is one more thing that can
go wrong. The MCP stdio transport is plain JSON-RPC 2.0 over stdin/stdout.

Usage (once, per agent):

    claude mcp add ovito-agent --scope user -- python3 /abs/path/to/ovito_agent_mcp.py

The socket to connect to comes from the OVITO_AGENT_SOCKET environment variable, which
OVITO sets for the agent process it launches. When the agent is started from a normal
terminal instead, pass --socket or set the variable by hand.
"""

import argparse
import base64
import json
import os
import socket
import sys
import threading

PROTOCOL_VERSION = "2024-11-05"
SERVER_NAME = "ovito-agent"
SERVER_VERSION = "0.1.0"


class BridgeError(RuntimeError):
    """Raised when the OVITO side reports a failed request."""


class BridgeConnection:
    """A newline-delimited JSON-RPC connection to the OVITO session bridge."""

    def __init__(self, socket_path):
        self.socket_path = socket_path
        self._socket = None
        self._file = None
        self._next_id = 1
        # The MCP server may serve concurrent tool calls; the bridge answers in order,
        # so requests are serialized here rather than interleaved on the wire.
        self._lock = threading.Lock()

    def _connect(self):
        if self._socket is not None:
            return
        if not self.socket_path:
            raise BridgeError(
                "No OVITO session socket configured. Start the agent from the AI Agent "
                "panel in OVITO, or set OVITO_AGENT_SOCKET to the socket path shown there."
            )
        if not os.path.exists(self.socket_path):
            raise BridgeError(
                f"The OVITO session socket {self.socket_path} does not exist. "
                "The OVITO window it belonged to has probably been closed."
            )
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(self.socket_path)
        self._socket = sock
        self._file = sock.makefile("rwb")

    def close(self):
        if self._file is not None:
            self._file.close()
            self._file = None
        if self._socket is not None:
            self._socket.close()
            self._socket = None

    def call(self, method, params=None):
        """Sends one request and returns the result object."""
        with self._lock:
            try:
                self._connect()
                request = {"id": self._next_id, "method": method, "params": params or {}}
                self._next_id += 1
                self._file.write((json.dumps(request) + "\n").encode("utf-8"))
                self._file.flush()
                line = self._file.readline()
            except (OSError, BrokenPipeError) as error:
                # A dropped connection usually means OVITO exited; reconnect on the next call.
                self.close()
                raise BridgeError(f"Lost the connection to the OVITO session: {error}")

            if not line:
                self.close()
                raise BridgeError("The OVITO session closed the connection.")

            response = json.loads(line.decode("utf-8"))
            if not response.get("ok"):
                raise BridgeError(response.get("error", "The OVITO session reported an error."))
            response.pop("ok", None)
            response.pop("id", None)
            return response


# Each entry maps one MCP tool to one bridge method. The descriptions are what the agent
# reads to decide which tool to reach for, so they say what the tool is *for*, not just
# what it does.
TOOLS = [
    {
        "name": "ovito_session_info",
        "method": "session.info",
        "description": "Report which OVITO session this connection reaches: version, process id and "
                       "loaded session file. Call this first to confirm the bridge is alive.",
        "schema": {"type": "object", "properties": {}},
    },
    {
        "name": "ovito_list_pipelines",
        "method": "scene.list",
        "description": "List the data pipelines in the scene with their modifier stacks. Modifier index 0 "
                       "is the topmost modifier, the one applied last. Start here to see what the user has built.",
        "schema": {"type": "object", "properties": {}},
    },
    {
        "name": "ovito_describe_pipeline",
        "method": "pipeline.describe",
        "description": "Describe one pipeline in full, including every parameter of every modifier and of the "
                       "file source. Use it before changing a parameter, to learn its exact name and current value.",
        "schema": {
            "type": "object",
            "properties": {
                "pipeline": {"description": "Pipeline index or name. Defaults to the one selected in the GUI."},
            },
        },
    },
    {
        "name": "ovito_select_pipeline",
        "method": "pipeline.select",
        "description": "Make a pipeline the selected one in OVITO's user interface, so the user sees the same "
                       "object you are working on.",
        "schema": {
            "type": "object",
            "properties": {"pipeline": {"description": "Pipeline index or name."}},
            "required": ["pipeline"],
        },
    },
    {
        "name": "ovito_delete_pipeline",
        "method": "pipeline.delete",
        "description": "Remove a pipeline from the scene. The user can undo this from OVITO's edit menu.",
        "schema": {
            "type": "object",
            "properties": {"pipeline": {"description": "Pipeline index or name."}},
            "required": ["pipeline"],
        },
    },
    {
        "name": "ovito_modifier_catalog",
        "method": "modifier.catalog",
        "description": "List the modifier classes this OVITO build provides, optionally only those applicable to "
                       "the current data, and optionally with their default parameters. Use it to find the exact "
                       "class name before inserting a modifier.",
        "schema": {
            "type": "object",
            "properties": {
                "filter": {"type": "string", "description": "Only list modifiers whose name contains this text."},
                "applicable_only": {"type": "boolean", "description": "Only list modifiers applicable to the current pipeline output."},
                "include_params": {"type": "boolean", "description": "Include each modifier's default parameters."},
                "pipeline": {"description": "Pipeline to test applicability against."},
            },
        },
    },
    {
        "name": "ovito_insert_modifier",
        "method": "modifier.insert",
        "description": "Insert a modifier at the top of a pipeline, optionally setting parameters right away. "
                       "The new modifier ends up at index 0.",
        "schema": {
            "type": "object",
            "properties": {
                "class": {"type": "string", "description": "Modifier class name, e.g. CommonNeighborAnalysisModifier."},
                "params": {"type": "object", "description": "Parameter values to apply to the new modifier."},
                "pipeline": {"description": "Pipeline index or name."},
            },
            "required": ["class"],
        },
    },
    {
        "name": "ovito_set_modifier_params",
        "method": "modifier.set_params",
        "description": "Change parameters of an existing modifier. Returns the modifier's full parameter set "
                       "afterwards, and names any values that were rejected.",
        "schema": {
            "type": "object",
            "properties": {
                "index": {"type": "integer", "description": "Modifier index, 0 being the topmost."},
                "params": {"type": "object", "description": "Parameter names mapped to new values."},
                "pipeline": {"description": "Pipeline index or name."},
            },
            "required": ["index", "params"],
        },
    },
    {
        "name": "ovito_set_modifier_enabled",
        "method": "modifier.set_enabled",
        "description": "Enable or disable a modifier without deleting it, which is the quickest way to see what "
                       "it contributes.",
        "schema": {
            "type": "object",
            "properties": {
                "index": {"type": "integer"},
                "enabled": {"type": "boolean"},
                "pipeline": {"description": "Pipeline index or name."},
            },
            "required": ["index", "enabled"],
        },
    },
    {
        "name": "ovito_delete_modifier",
        "method": "modifier.delete",
        "description": "Delete a modifier from a pipeline.",
        "schema": {
            "type": "object",
            "properties": {
                "index": {"type": "integer"},
                "pipeline": {"description": "Pipeline index or name."},
            },
            "required": ["index"],
        },
    },
    {
        "name": "ovito_data_summary",
        "method": "data.summary",
        "description": "Evaluate a pipeline and describe what comes out: particles, bonds, surfaces, dislocation "
                       "networks, data tables, their element counts and property names, plus all global attributes.",
        "schema": {
            "type": "object",
            "properties": {
                "pipeline": {"description": "Pipeline index or name."},
                "frame": {"type": "integer", "description": "Animation frame. Defaults to the frame on screen."},
            },
        },
    },
    {
        "name": "ovito_data_attributes",
        "method": "data.attributes",
        "description": "Return only the global attributes of a pipeline's output, e.g. counts computed by an "
                       "analysis modifier. Cheaper than a full summary when that is all you need.",
        "schema": {
            "type": "object",
            "properties": {
                "pipeline": {"description": "Pipeline index or name."},
                "frame": {"type": "integer"},
            },
        },
    },
    {
        "name": "ovito_data_table",
        "method": "data.table",
        "description": "Read a data table produced by the pipeline, such as a histogram, an RDF or a time series, "
                       "as x values and named columns.",
        "schema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Table identifier. Omit to get the first table."},
                "max_rows": {"type": "integer", "description": "Row cap, default 1000."},
                "pipeline": {"description": "Pipeline index or name."},
                "frame": {"type": "integer"},
            },
        },
    },
    {
        "name": "ovito_data_elements",
        "method": "data.elements",
        "description": "Read per-element property values (particle positions, structure types, dislocation "
                       "properties, ...) for a range of elements. Ask for the properties you need and keep the "
                       "count small: this is for spot checks, not for bulk data transfer.",
        "schema": {
            "type": "object",
            "properties": {
                "container": {"type": "string", "description": "Property container, e.g. 'particles' or 'bonds'."},
                "properties": {"type": "array", "items": {"type": "string"}, "description": "Property names. Omit for all."},
                "start": {"type": "integer", "description": "Index of the first element, default 0."},
                "count": {"type": "integer", "description": "Number of elements, default 100, maximum 10000."},
                "pipeline": {"description": "Pipeline index or name."},
                "frame": {"type": "integer"},
            },
        },
    },
    {
        "name": "ovito_import_file",
        "method": "file.import",
        "description": "Import a simulation file into the scene. The file format is detected automatically, and a "
                       "file that is part of a numbered sequence is loaded as a trajectory.",
        "schema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Absolute path of the file to import."},
                "mode": {"type": "string", "enum": ["add", "replace", "reset"],
                         "description": "add: keep the scene and add a pipeline (default); replace: replace the "
                                        "selected pipeline's input; reset: clear the scene first."},
                "autodetect_sequence": {"type": "boolean"},
            },
            "required": ["path"],
        },
    },
    {
        "name": "ovito_animation_info",
        "method": "animation.info",
        "description": "Report the animation interval of the scene and which frame is currently shown.",
        "schema": {"type": "object", "properties": {}},
    },
    {
        "name": "ovito_goto_frame",
        "method": "animation.goto",
        "description": "Jump to an animation frame. The viewports and the user's view follow along.",
        "schema": {
            "type": "object",
            "properties": {"frame": {"type": "integer"}},
            "required": ["frame"],
        },
    },
    {
        "name": "ovito_list_viewports",
        "method": "viewport.list",
        "description": "List the viewports with their camera position, direction, field of view and view type.",
        "schema": {"type": "object", "properties": {}},
    },
    {
        "name": "ovito_set_camera",
        "method": "viewport.camera",
        "description": "Aim a viewport camera: set its position, viewing direction, field of view, projection type "
                       "or render preview mode, or frame the whole scene with zoom_all.",
        "schema": {
            "type": "object",
            "properties": {
                "viewport": {"description": "Viewport index or title. Defaults to the active viewport."},
                "camera_position": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
                "camera_direction": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
                "field_of_view": {"type": "number"},
                "view_type": {"type": "integer", "description": "0 none, 1 top, 2 bottom, 3 front, 4 back, 5 left, 6 right, 7 ortho, 8 perspective."},
                "render_preview": {"type": "boolean"},
                "zoom_all": {"type": "boolean", "description": "Frame the whole scene."},
            },
        },
    },
]

SCREENSHOT_TOOL = {
    "name": "ovito_screenshot",
    "method": "viewport.screenshot",
    "description": "Capture what the user currently sees -- one viewport, all viewports, or the whole OVITO "
                   "window -- and return it as an image. Use it to check your own work and to answer questions "
                   "about how something looks.",
    "schema": {
        "type": "object",
        "properties": {
            "viewport": {"description": "Viewport index or title, 'all' for the viewport area, or 'window' for the whole window."},
            "path": {"type": "string", "description": "Optional file to write the PNG to."},
        },
    },
}


class McpServer:
    """A minimal MCP server speaking JSON-RPC 2.0 over stdin/stdout."""

    def __init__(self, bridge):
        self.bridge = bridge
        self.tools = {tool["name"]: tool for tool in TOOLS}
        self.tools[SCREENSHOT_TOOL["name"]] = SCREENSHOT_TOOL

    def run(self):
        for line in sys.stdin.buffer:
            line = line.strip()
            if not line:
                continue
            try:
                message = json.loads(line.decode("utf-8"))
            except json.JSONDecodeError:
                continue
            response = self.handle(message)
            # Notifications carry no id and expect no answer.
            if response is not None:
                sys.stdout.write(json.dumps(response) + "\n")
                sys.stdout.flush()

    def handle(self, message):
        method = message.get("method")
        message_id = message.get("id")
        if message_id is None:
            return None

        try:
            if method == "initialize":
                result = {
                    "protocolVersion": PROTOCOL_VERSION,
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                }
            elif method == "tools/list":
                result = {
                    "tools": [
                        {
                            "name": tool["name"],
                            "description": tool["description"],
                            "inputSchema": tool["schema"],
                        }
                        for tool in self.tools.values()
                    ]
                }
            elif method == "tools/call":
                result = self.call_tool(message.get("params") or {})
            elif method == "ping":
                result = {}
            else:
                return {
                    "jsonrpc": "2.0",
                    "id": message_id,
                    "error": {"code": -32601, "message": f"Method not found: {method}"},
                }
        except Exception as error:  # noqa: BLE001 - the protocol wants errors as data.
            return {
                "jsonrpc": "2.0",
                "id": message_id,
                "error": {"code": -32603, "message": str(error)},
            }

        return {"jsonrpc": "2.0", "id": message_id, "result": result}

    def call_tool(self, params):
        name = params.get("name")
        arguments = params.get("arguments") or {}
        tool = self.tools.get(name)
        if tool is None:
            return {
                "content": [{"type": "text", "text": f"Unknown tool: {name}"}],
                "isError": True,
            }
        try:
            result = self.bridge.call(tool["method"], arguments)
        except BridgeError as error:
            # Tool failures are reported as content, not as protocol errors, so the agent
            # can read the message and correct itself.
            return {"content": [{"type": "text", "text": str(error)}], "isError": True}

        if name == SCREENSHOT_TOOL["name"]:
            return self.screenshot_content(result)
        return {"content": [{"type": "text", "text": json.dumps(result, indent=2)}]}

    @staticmethod
    def screenshot_content(result):
        path = result.get("path")
        try:
            with open(path, "rb") as image_file:
                encoded = base64.b64encode(image_file.read()).decode("ascii")
        except OSError as error:
            return {
                "content": [{"type": "text", "text": f"The screenshot could not be read back: {error}"}],
                "isError": True,
            }
        return {
            "content": [
                {"type": "image", "data": encoded, "mimeType": "image/png"},
                {"type": "text", "text": f"{result.get('width')}x{result.get('height')} screenshot saved to {path}"},
            ]
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", default=os.environ.get("OVITO_AGENT_SOCKET", ""),
                        help="Path of the OVITO session socket. Defaults to $OVITO_AGENT_SOCKET.")
    parser.add_argument("--call", metavar="METHOD",
                        help="Call one bridge method and print the response, instead of serving MCP. For debugging.")
    parser.add_argument("--params", default="{}", help="JSON parameters for --call.")
    args = parser.parse_args()

    bridge = BridgeConnection(args.socket)

    if args.call:
        try:
            print(json.dumps(bridge.call(args.call, json.loads(args.params)), indent=2))
        except BridgeError as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        return 0

    McpServer(bridge).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())

local rainrate = [[
    {
        "id": "radioamber53161.rainrate",
        "version": 1,
        "status": "proposed",
        "name": "rainrate",
        "ephemeral": false,
        "attributes": {
            "rate": {
                "schema": {
                    "type": "object",
                    "properties": {
                        "value": {
                            "type": "number",
                            "minimum": 0
                        },
                        "unit": {
                            "type": "string",
                            "enum": [
                                "in/hr"
                            ],
                            "default": "in/hr"
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "value"
                    ]
                },
                "enumCommands": []
            }
        },
        "commands": {}
    }
]]

local raintotal = [[
    {
        "id": "radioamber53161.raintotal",
        "version": 1,
        "status": "proposed",
        "name": "raintotal",
        "ephemeral": false,
        "attributes": {
            "total": {
                "schema": {
                    "type": "object",
                    "properties": {
                        "value": {
                            "type": "number",
                            "minimum": 0
                        },
                        "unit": {
                            "type": "string",
                            "enum": [
                                "Inches"
                            ],
                            "default": "Inches"
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "value"
                    ]
                },
                "enumCommands": []
            }      
        },
        "commands": {}
    }
]]

local datetime = [[
    {
        "id": "radioamber53161.datetime",
        "version": 1,
        "status": "proposed",
        "name": "datetime",
        "ephemeral": false,
        "attributes": {
            "datetime": {
                "schema": {
                    "type": "object",
                    "properties": {
                        "value": {
                            "type": "string"
                        }
                    },
                    "additionalProperties": false,
                    "required": []
                },
                "enumCommands": []
            }      
        },
        "commands": {}
    }
]]

return {
	rainrate = rainrate,
    raintotal = raintotal,
    datetime = datetime
}
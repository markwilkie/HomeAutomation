local windspeed = [[
    {
        "id": "radioamber53161.windspeed",
        "version": 1,
        "status": "proposed",
        "name": "windspeed",
        "ephemeral": false,
        "attributes": {
            "speed": {
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
                                "KTS"
                            ],
                            "default": "KTS"
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

local winddirection = [[
    {
        "id": "radioamber53161.winddirection",
        "version": 1,
        "status": "proposed",
        "name": "winddirection",
        "ephemeral": false,
        "attributes": {
            "direction": {
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
                                "DEG"
                            ],
                            "default": "DEG"
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

local windgust = [[
    {
        "id": "radioamber53161.windgust",
        "version": 1,
        "status": "proposed",
        "name": "windgust",
        "ephemeral": false,
        "attributes": {
            "gust": {
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
                                "KTS"
                            ],
                            "default": "KTS"
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
	windspeed = windspeed,
    winddirection = winddirection,
    windgust = windgust,
    datetime = datetime
}
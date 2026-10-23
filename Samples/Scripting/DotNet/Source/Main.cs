// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Genesis.Scripting;
public sealed class Main : IScriptModule
{
    private int count;
    public JsonElement Call(string function, JsonElement arguments, IScriptHost host) => function switch
    {
        "OnBeginPlay" => Begin(arguments,host),
        "OnTick" => SetName(arguments,host,$"Script Tick {++count}"),
        "OnEvent" => arguments.GetProperty("Name").GetString()=="Rename" ? SetName(arguments,host,arguments.GetProperty("Payload").GetProperty("Name").GetString()!) : JsonSerializer.SerializeToElement<object?>(null),
        "OnEndPlay" => host.Invoke("Log.Write",JsonSerializer.SerializeToElement(new {Message=arguments.GetProperty("Reason").GetString()})),
        "RunScenario" => RunScenario(arguments,host),
        "BuildTerrain" => BuildTerrain(arguments, host),
        "Echo" => host.Invoke("Echo", arguments),
        "BuildWorld" => host.Invoke("World.SpawnActor",JsonSerializer.SerializeToElement(new {Name="C# Example Actor"})).GetProperty("Id").Clone(),
        "Step" => JsonSerializer.SerializeToElement(++count),
        "Fail" => throw new InvalidOperationException("Expected C# failure"),
        "Invoke" => host.Invoke(arguments.GetProperty("API").GetString()!, arguments.GetProperty("Arguments")),
        _ => throw new MissingMethodException($"Unknown script function: {function}")
    };
    private static JsonElement BuildTerrain(JsonElement args, IScriptHost host)
    {
        JsonElement Call(string api, object value) => host.Invoke(api, JsonSerializer.SerializeToElement(value));
        Call("Editor.BeginTransaction", new {Description="Build Terrain Feature Project"});
        var land=Call("Landscape.Create", new {QuadsX=32,QuadsY=32,QuadSize=32,Position=new[]{0,0,0}}).GetProperty("ActorId").GetString()!;
        Call("Landscape.AddLayer",new {ActorId=land,Name="Grass",Color=new[]{80,150,70}});
        Call("Landscape.ApplyBrush",new {ActorId=land,Tool="Sculpt",Position=new[]{0,0,0},Radius=256,Strength=0.4,Falloff=0.5,Seconds=0.5});
        Call("Landscape.ApplyBrush",new {ActorId=land,Tool="Paint",Position=new[]{0,0,0},Radius=256,Strength=0.8,Falloff=0.5,Seconds=0.5,Layer=0});
        var foliage=Call("Foliage.AddPreset",new {Preset="Grass"}).GetProperty("ActorId").GetString()!;
        if(!Call("Foliage.Place",new {Type=0,Position=new[]{0,0,0}}).GetBoolean())throw new InvalidOperationException("Could not place foliage");
        Call("Editor.EndTransaction",new {});
        if(Call("Landscape.GetInfo",new {ActorId=land}).GetProperty("MaxHeight").GetDouble()<=0)throw new InvalidOperationException("Sculpt failed");
        if(Call("Foliage.GetInfo",new {ActorId=foliage}).GetProperty("Instances").GetInt32()!=1)throw new InvalidOperationException("Foliage count mismatch");
        if(args.TryGetProperty("Output",out var output))Call("Editor.Save",new {Path=output.GetString()});
        return JsonSerializer.SerializeToElement(new {Landscape=land,Foliage=foliage});
    }
    private JsonElement Begin(JsonElement args, IScriptHost host) {count=0;return SetName(args,host,"Script Begin");}
    private static JsonElement SetName(JsonElement args,IScriptHost host,string name) => host.Invoke("World.SetActorName",JsonSerializer.SerializeToElement(new {ActorId=args.GetProperty("ActorId").GetString(),Name=name}));

    private static JsonElement RunScenario(JsonElement arguments,IScriptHost host)
    {
        var args=JsonNode.Parse(arguments.GetRawText())!;
        var variables=new Dictionary<string,JsonNode?>{{"Args",args}};
        var types=host.Invoke("Material.Describe",JsonSerializer.SerializeToElement(new {})).GetProperty("Expressions").EnumerateArray().ToDictionary(e=>e.GetProperty("Name").GetString()!,e=>e.GetProperty("Id").GetInt32());
        JsonNode? Get(string path) {var parts=path.Split('.');var current=variables[parts[0]];foreach(var key in parts.Skip(1))current=current is JsonArray array?array[int.Parse(key)]:current![key];return current;}
        JsonNode? Resolve(JsonNode? value) {
            if(value is JsonObject obj){var result=new JsonObject();foreach(var item in obj)result[item.Key]=item.Key=="Expression"&&item.Value is JsonValue v&&v.TryGetValue<string>(out var type)?JsonValue.Create(types[type]):Resolve(item.Value);return result;}
            if(value is JsonArray array){var result=new JsonArray();foreach(var item in array)result.Add(Resolve(item));return result;}
            if(value is JsonValue text&&text.TryGetValue<string>(out var str)){if(str.StartsWith('$')&&!str.StartsWith("${"))return Get(str[1..])?.DeepClone();return JsonValue.Create(Regex.Replace(str,@"\$\{([\w.]+)\}",m=>Get(m.Groups[1].Value)!.GetValue<string>()));}
            return value?.DeepClone();
        }
        foreach(var step in args["Steps"]!.AsArray()) {
            var api=step!["API"]!.GetValue<string>();var result=JsonNode.Parse(host.Invoke(api,JsonSerializer.SerializeToElement(Resolve(step["Arguments"])??new JsonObject())).GetRawText());
            if(step["Capture"] is JsonValue capture)variables[capture.GetValue<string>()]=result;
            if(step["Positive"] is JsonArray positive)foreach(var key in positive)if(result![key!.GetValue<string>()]!.GetValue<double>()<=0)throw new InvalidOperationException($"{api}: expected positive value");
            if(step["Expect"] is JsonArray checks)foreach(var check in checks){var actual=result;foreach(var key in check!["Path"]!.GetValue<string>().Split('.'))actual=actual is JsonArray a?a[int.Parse(key)]:actual![key];if(!JsonNode.DeepEquals(actual,Resolve(check["Value"])))throw new InvalidOperationException($"{api}: expectation mismatch");}
        }
        return JsonSerializer.SerializeToElement(Resolve(args["Return"]));
    }
}

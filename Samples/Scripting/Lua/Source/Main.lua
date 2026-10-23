-- Shared host API; this module runs without native engine pointers.
local ticks = 0
local function RunScenario(args)
    local variables={Args=args}
    local expressions={}
    for _,e in ipairs(Genesis.invoke('Material.Describe',{}).Expressions) do expressions[e.Name]=e.Id end
    local function get(path)
        local value=variables
        for key in path:gmatch('[^.]+') do local number=tonumber(key);value=value[number and number+1 or key] end
        return value
    end
    local resolve
    resolve=function(value)
        if type(value)=='table' then
            for key,v in pairs(value) do
                if key=='Expression' and type(v)=='string' then value[key]=assert(expressions[v],'Unknown material expression') else value[key]=resolve(v) end
            end
            return value
        end
        if type(value)=='string' then
            if value:sub(1,1)=='$' and value:sub(1,2)~='${' then return get(value:sub(2)) end
            return (value:gsub('%${([%w_.]+)}',function(path) return tostring(get(path)) end))
        end
        return value
    end
    for _,step in ipairs(args.Steps) do
        local result=Genesis.invoke(step.API,resolve(step.Arguments or {}))
        if step.Capture then variables[step.Capture]=result end
        for _,key in ipairs(step.Positive or {}) do assert(result[key]>0,step.API..': '..key..' was not positive') end
        for _,check in ipairs(step.Expect or {}) do
            local actual=result
            for key in check.Path:gmatch('[^.]+') do local number=tonumber(key);actual=actual[number and number+1 or key] end
            assert(actual==resolve(check.Value),step.API..': '..check.Path..' mismatch')
        end
    end
    return resolve(args.Return)
end
return {
    RunScenario=RunScenario,
    BuildTerrain = function(args)
        local call = Genesis.invoke
        call('Editor.BeginTransaction', {Description='Build Terrain Feature Project'})
        local land = call('Landscape.Create', {QuadsX=32,QuadsY=32,QuadSize=32,Position={0,0,0}}).ActorId
        call('Landscape.AddLayer', {ActorId=land,Name='Grass',Color={80,150,70}})
        call('Landscape.ApplyBrush', {ActorId=land,Tool='Sculpt',Position={0,0,0},Radius=256,Strength=0.4,Falloff=0.5,Seconds=0.5})
        call('Landscape.ApplyBrush', {ActorId=land,Tool='Paint',Position={0,0,0},Radius=256,Strength=0.8,Falloff=0.5,Seconds=0.5,Layer=0})
        local foliage=call('Foliage.AddPreset',{Preset='Grass'}).ActorId
        assert(call('Foliage.Place',{Type=0,Position={0,0,0}}),'Could not place foliage')
        call('Editor.EndTransaction',{})
        assert(call('Landscape.GetInfo',{ActorId=land}).MaxHeight>0,'Sculpt failed')
        assert(call('Foliage.GetInfo',{ActorId=foliage}).Instances==1,'Foliage count mismatch')
        if args.Output then call('Editor.Save',{Path=args.Output}) end
        return {Landscape=land,Foliage=foliage}
    end,
    OnBeginPlay = function(args) ticks=0;Genesis.invoke('World.SetActorName',{ActorId=args.ActorId,Name='Script Begin'}) end,
    OnTick = function(args) ticks=ticks+1;Genesis.invoke('World.SetActorName',{ActorId=args.ActorId,Name='Script Tick '..ticks}) end,
    OnEvent = function(args) if args.Name=='Rename' then Genesis.invoke('World.SetActorName',{ActorId=args.ActorId,Name=args.Payload.Name}) end end,
    OnEndPlay = function(args) Genesis.invoke('Log.Write',{Message=args.Reason}) end,
    Invoke = function(args) return Genesis.invoke(args.API, args.Arguments) end,
    BuildWorld = function(args) return Genesis.invoke('World.SpawnActor', {Name='Lua Example Actor'}).Id end,
    Step = function(args) ticks=ticks+1;return ticks end
}

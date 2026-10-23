-- Shared host API; this module runs without native engine pointers.
local ticks = 0
return {
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

import genesis
_ticks = 0

def Invoke(args):
    return genesis.invoke(args['API'], args['Arguments'])

def BuildWorld(args):
    return genesis.invoke('World.SpawnActor', {'Name': 'Python Example Actor'})['Id']

def Step(args):
    global _ticks
    _ticks += 1
    return _ticks


def BuildTerrain(args):
    call = genesis.invoke
    call('Editor.BeginTransaction', {'Description': 'Build Terrain Feature Project'})
    land = call('Landscape.Create', {'QuadsX':32,'QuadsY':32,'QuadSize':32,'Position':[0,0,0]})['ActorId']
    call('Landscape.AddLayer', {'ActorId':land,'Name':'Grass','Color':[80,150,70]})
    call('Landscape.ApplyBrush', {'ActorId':land,'Tool':'Sculpt','Position':[0,0,0],'Radius':256,'Strength':0.4,'Falloff':0.5,'Seconds':0.5})
    call('Landscape.ApplyBrush', {'ActorId':land,'Tool':'Paint','Position':[0,0,0],'Radius':256,'Strength':0.8,'Falloff':0.5,'Seconds':0.5,'Layer':0})
    foliage = call('Foliage.AddPreset', {'Preset':'Grass'})['ActorId']
    assert call('Foliage.Place', {'Type':0,'Position':[0,0,0]}), 'Could not place foliage'
    call('Editor.EndTransaction', {})
    assert call('Landscape.GetInfo', {'ActorId':land})['MaxHeight'] > 0, 'Sculpt failed'
    assert call('Foliage.GetInfo', {'ActorId':foliage})['Instances'] == 1, 'Foliage count mismatch'
    if args.get('Output'):
        call('Editor.Save', {'Path':args['Output']})
    return {'Landscape':land, 'Foliage':foliage}


def OnBeginPlay(args):
    global _ticks
    _ticks = 0
    genesis.invoke('World.SetActorName', {'ActorId':args['ActorId'], 'Name':'Script Begin'})


def OnTick(args):
    global _ticks
    _ticks += 1
    genesis.invoke('World.SetActorName', {'ActorId':args['ActorId'], 'Name':f'Script Tick {_ticks}'})


def OnEvent(args):
    if args['Name'] == 'Rename':
        genesis.invoke('World.SetActorName', {'ActorId':args['ActorId'], 'Name':args['Payload']['Name']})


def OnEndPlay(args):
    genesis.invoke('Log.Write', {'Message':args['Reason']})

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


def RunScenario(args):
    """Execute the checked-in material authoring regression with the public API."""
    import re
    variables = {'Args': args}
    expressions = {e['Name']: e['Id'] for e in genesis.invoke('Material.Describe', {})['Expressions']}

    def get(path):
        value = variables
        for key in path.split('.'):
            value = value[int(key)] if isinstance(value, list) else value[key]
        return value

    def resolve(value):
        if isinstance(value, dict):
            return {k: expressions[v] if k == 'Expression' and isinstance(v, str) else resolve(v) for k, v in value.items()}
        if isinstance(value, list):
            return [resolve(v) for v in value]
        if isinstance(value, str):
            if value.startswith('$') and not value.startswith('${'):
                return get(value[1:])
            return re.sub(r'\$\{([\w.]+)\}', lambda m: str(get(m.group(1))), value)
        return value

    for step in args['Steps']:
        result = genesis.invoke(step['API'], resolve(step.get('Arguments', {})))
        if step.get('Capture'):
            variables[step['Capture']] = result
        for key in step.get('Positive', []):
            assert result[key] > 0, f"{step['API']}: {key} was not positive"
        for check in step.get('Expect', []):
            actual = result
            for key in check['Path'].split('.'):
                actual = actual[int(key)] if isinstance(actual, list) else actual[key]
            assert actual == resolve(check['Value']), f"{step['API']}: {check['Path']} mismatch"
    return resolve(args['Return'])

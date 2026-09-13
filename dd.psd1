@{
    schema = 1
    project = @{
        name = 'stuntcarracer'
        type = 'gui'
        'default-target' = 'app'
    }
    dependencies = @{ owner = 'dd' }
    build = @{
        'x64-windows' = @{
            debug = 'debug'
            release = 'release'
        }
    }
    targets = @(
        @{
            id = 'app'
            kind = 'gui'
            'cmake-target' = 'stuntcarracer'
            'test-label' = 'stuntcarracer'
            'debug-path' = 'exe/stunt-car-racer-64d{exe}'
            'release-path' = 'exe/stunt-car-racer-64{exe}'
            platforms = @('x64-windows')
        }
    )
}

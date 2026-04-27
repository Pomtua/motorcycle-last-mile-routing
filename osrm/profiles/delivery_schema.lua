local delivery_points = osm2pgsql.define_table({
    name = 'delivery_pool',
    ids = { type = 'any', id_column = 'osm_id' },
    columns = {
        { column = 'name',     type = 'text' }, 
        { column = 'type',     type = 'text' }, 
        { column = 'location', type = 'point', projection = 4326, not_null = true }
    }
})

function osm2pgsql.process_node(object)
    if object.tags.amenity or object.tags.shop or object.tags.building then
        delivery_points:insert({
            name = object.tags.name,
            type = object.tags.amenity or object.tags.shop or object.tags.building,
            location = object:as_point()
        })
    end
end

function osm2pgsql.process_way(object)
    if object.tags.building then
        local geom = object:as_polygon():centroid()
        if geom then
            delivery_points:insert({
                name = object.tags.name,
                type = object.tags.building,
                location = geom
            })
        end
    end
end
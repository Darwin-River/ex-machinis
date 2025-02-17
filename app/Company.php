<?php

namespace App;

use Illuminate\Database\Eloquent\Model;

class Company extends Model
{
    const INITIAL_AGENTS = 3;
    // Define some constants to define the objects ID for drones orbits
    // The 3 initial agents will one in each station orbit
    const EARTH_OBJECT_ID = 3;
    const EARTH_STATION_OBJECT_ID = 187;
    const MARS_STATION_OBJECT_ID = 188;
    const JUPITER_STATION_OBJECT_ID = 189;
    // Constant to identify the default hull type
    const DEFAULT_DRONE_HULL_TYPE = 1;

    //
    protected $fillable = [
        'name', 'user_id'
    ];
    public $timestamps = false;

    /**
     * Get the user associated with the company.
     */
    public function user()
    {
        return $this->belongsTo('App\User');
    }

    /**
     * Get the agents associated with the company.
     */
    public function agents()
    {
        return $this->hasMany('App\Agent');
    }

    /**
     * Creates the initial drones for a company.
     * @return array of Agent objects
     *
     */
    public function generateStartingDrones($user_id)
    {
        $orbits = [ Company::EARTH_OBJECT_ID, Company::MARS_STATION_OBJECT_ID, Company::JUPITER_STATION_OBJECT_ID ];
        $companyDrones = [];
        for ($i = 0; $i < Company::INITIAL_AGENTS; $i++) {
            $agent = new Agent();
            $agent->assignRandomName();
            $agent->user_id = $user_id;
            //set the orbit
            $agent->object_id = $orbits[$i];
            $agent->hull_type = Company::DEFAULT_DRONE_HULL_TYPE;
            $agent->save();
            $companyDrones[] = $agent;

            $agent->addCargo(5901); // Adds Starter Kit I.

        }
        return $companyDrones;
    }

}
